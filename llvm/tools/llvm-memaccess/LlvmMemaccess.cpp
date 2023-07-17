#include <unordered_map>
#include <vector>

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/LinkAllIR.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/InitializePasses.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/BinaryFormat/Dwarf.h"

using namespace llvm;

namespace {

cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));

class DumperPass : public PassInfoMixin<DumperPass> {
public:
  DumperPass() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

struct FieldFrequency {
  unsigned Offset;
  float ReadFreq;
  float WriteFreq;

  bool operator <(const FieldFrequency& O) const {
    return Offset < O.Offset;
  }
};

struct TypeFieldFrequencies {
  TypeFieldFrequencies() {}

  FieldFrequency& FreqForOffset(unsigned Offset) {
    FrequenciesType::iterator I = std::lower_bound(Frequencies.begin(),
                                                   Frequencies.end(), Offset,
                     [](const FieldFrequency &F, unsigned Offset) {
                      return F.Offset < Offset;
                     });
    if (I->Offset != Offset) {
      I = Frequencies.insert(I, FieldFrequency());
    }
    return *I;
  }
  using FrequenciesType = SmallVector<FieldFrequency>;
  FrequenciesType Frequencies;
};

struct Guess {
  StructType *llvm_type = nullptr;
  DICompositeType *dwarf_type = nullptr;
  APInt Offset;
};

static bool tryLLVMType(Guess *G, llvm::Type *type) {
  StructType* ST = dyn_cast<StructType>(type);
  if (ST == nullptr)
    return false;
  if (ST->isLiteral()) {
    // TODO: should we continue hoping we find a literal type later?
  } else {
    G->llvm_type = ST;
    G->dwarf_type = nullptr;
    return true;
  }
  return false;
}

static const Value *guessGEP(const DataLayout &DL, const GetElementPtrInst &GEP,
                             Guess *G) {
  unsigned AS = GEP.getAddressSpace();
  APInt Offset = APInt::getZero(DL.getPointerSizeInBits(AS));
  if (!GEP.accumulateConstantOffset(DL, Offset))
    return nullptr;
  Type* PointeeType = GEP.getSourceElementType();
  if (tryLLVMType(G, PointeeType)) {
    G->Offset += Offset;
    return GEP.getPointerOperand();
  }
  return nullptr;
}

void guessAlloca(const DataLayout &DL, const AllocaInst &AI, Guess *G) {
  Type* type = AI.getAllocatedType();
  tryLLVMType(G, type);
}

static DIType *skipDerived(DIType *Type) {
  DIDerivedType *Derived = dyn_cast<DIDerivedType>(Type);
  if (Derived == nullptr)
    return Type;
  unsigned tag = Derived->getTag();
  if (tag == dwarf::DW_TAG_const_type ||
      tag == dwarf::DW_TAG_restrict_type ||
      tag == dwarf::DW_TAG_volatile_type ||
      tag == dwarf::DW_TAG_packed_type ||
      tag == dwarf::DW_TAG_atomic_type ||
      tag == dwarf::DW_TAG_immutable_type) {
    DIType *Base = Derived->getBaseType();
    if (Base == nullptr)
      return Type;
    return skipDerived(Base);
  }
  return Type;
}

static bool tryDwarfType(DIType *Type, Guess *G) {
  DIDerivedType *TypeD = dyn_cast<DIDerivedType>(skipDerived(Type));
  if (TypeD == nullptr)
    return false;
  unsigned tag = TypeD->getTag();
  if (tag != dwarf::DW_TAG_pointer_type &&
      tag != dwarf::DW_TAG_reference_type &&
      tag != dwarf::DW_TAG_rvalue_reference_type)
    return false;
  DIType* Base = TypeD->getBaseType();
  if (Base == nullptr)
    return false;
  DIType *BaseS = skipDerived(Base);
  DICompositeType *BaseC = dyn_cast<DICompositeType>(BaseS);
  if (BaseC == nullptr)
    return false;
  G->llvm_type = nullptr;
  G->dwarf_type = BaseC;
  return true;
}

static void dumpEscaped(StringRef S) {
  size_t I = 0;
  for (;;) {
    size_t next_backslash = S.find('\\', I);
    size_t next_quote = S.find('"', I);
    size_t next_special = std::min(next_backslash, next_quote);
    if (next_special == StringRef::npos) {
      outs() << S.substr(I);
      break;
    } else {
      outs() << S.substr(I, next_special - I);
    }
    outs() << '\\' << S[next_special];
    I = next_special + 1;
  }
}

static void dumpString(StringRef S) {
  outs() << '\"';
  dumpEscaped(S);
  outs() << '\"';
}

static void dumpDwarfTypeIdent(const DICompositeType &CT) {
  if (CT.getTag() == dwarf::DW_TAG_array_type) {
    // not really handled yet...
    outs() << "\"$array\"";
    return;
  }
  StringRef OdrIdent = CT.getIdentifier();
  if (OdrIdent.size() > 0) {
    dumpString(OdrIdent);
    return;
  }
  // It's a local type.
  // hack: for now let's just assume there's no two types with the same name
  // defined on the same line...
  assert(CT.getLine() > 0);
  outs() << "\"L.";
  dumpEscaped(CT.getName());
  outs() << ':' << CT.getLine() << "\"";
}

static void guessCall(const CallBase &Call, Guess *G) {
  Function *Callee = Call.getCalledFunction();
  if (Callee == nullptr)
    return;
  DISubprogram *SP = Callee->getSubprogram();
  if (SP == nullptr)
    return;
  DISubroutineType *Type = SP->getType();
  if (Type == nullptr)
    return;
  DITypeRefArray Types = Type->getTypeArray();
  if (Types.size() == 0)
    return;
  DIType *RetType = Types[0];
  tryDwarfType(RetType, G);
}

static const DbgVariableIntrinsic *findDebugValueFor(const BasicBlock &BB,
                                                     const Value &V) {
  for (const Instruction &I : BB) {
    const DbgVariableIntrinsic *DbgVar = dyn_cast<DbgVariableIntrinsic>(&I);
    if (DbgVar == nullptr)
      continue;
    const Value *DbgValue = DbgVar->getVariableLocationOp(0);
    if (DbgValue == &V)
      return DbgVar;
  }
  return nullptr;
}

static void guessArgument(const Argument &Arg, Guess *G) {
  // argument numbers in IR do not necessarily match argument numbers in
  // C++/dwarf. So we just searhch for corresponding llvm.dbg.value and use
  // this dwarf type.
  const BasicBlock &FirstBlock = Arg.getParent()->front();
  const DbgVariableIntrinsic *DbgVar = findDebugValueFor(FirstBlock, Arg);
  if (DbgVar == nullptr)
    return;

  if (DbgVar->getIntrinsicID() == Intrinsic::dbg_value) {
    DILocalVariable *Var = DbgVar->getVariable();
    tryDwarfType(Var->getType(), G);
  } else {
    // TODO: dbg_declare for structs passed by val?
  }
}

void dumpTypeGuess(const DataLayout &DL, const Value &Ptr) {
  Guess G;
  G.Offset = APInt::getZero(64);

  const Value* P = &Ptr;
  while (P) {
    if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(P)) {
      P = guessGEP(DL, *GEP, &G);
    } else if (const AllocaInst *AI = dyn_cast<AllocaInst>(P)) {
      guessAlloca(DL, *AI, &G);
      break;
    } else if (const CallBase *Call = dyn_cast<CallBase>(P)) {
      guessCall(*Call, &G);
      break;
    } else if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(P)) {
      tryLLVMType(&G, GV->getValueType());
      break;
    } else if (const Argument *Arg = dyn_cast<Argument>(P)) {
      guessArgument(*Arg, &G);
      break;
    } else {
      break;
    }
  }

  if (G.dwarf_type != nullptr) {
    outs() << ",\n      \"trace_type\": ";
    dumpDwarfTypeIdent(*G.dwarf_type);
    outs() << ",\n      \"trace_offset\": " << G.Offset;
  } else if (G.llvm_type != nullptr) {
    outs() << ",\n      \"trace_type\": \"" << G.llvm_type->getStructName()
      << "\"";
    outs() << ",\n      \"trace_offset\": " << G.Offset;
  }
}

void dumpAA(const Instruction &I) {
  AAMDNodes AAMD = I.getAAMetadata();
  const MDNode *TBAA = AAMD.TBAA;
  if (TBAA == nullptr)
    return;
  const MDNode &BaseTy = cast<MDNode>(*TBAA->getOperand(0));
  const MDString &BaseTyName = cast<MDString>(*BaseTy.getOperand(0));
  outs() << ",\n      \"aa_struct\": \"" << BaseTyName.getString() << "\"";
  const MDNode &AccessTy = cast<MDNode>(*TBAA->getOperand(1));
  const MDString &AccessTyName = cast<MDString>(*AccessTy.getOperand(0));
  outs() << ",\n      \"aa_access_type\": \"" << AccessTyName.getString() << "\"";
  const ConstantAsMetadata &Offset = cast<ConstantAsMetadata>(*TBAA->getOperand(2));
  outs() << ",\n      \"aa_offset\": " << cast<ConstantInt>(Offset.getValue())->getSExtValue();
}

void dumpOp(const DataLayout &DL, Type* OpType, Align A) {
  TypeSize Sz = DL.getTypeStoreSize(OpType);
  outs() << ",\n      \"size_bytes\": " << Sz.getFixedValue();
  outs() << ",\n      \"align_bytes\": " << A.value();
}

void dumpCompositeType(const DICompositeType &CT) {
  outs() << "    {";
  outs() << "\n      \"name\": ";
  dumpString(CT.getName());
  outs() << ",\n      \"file\": ";
  dumpString(CT.getFilename());
  outs() << ",\n      \"line\": " << CT.getLine();
  outs() << ",\n      \"ident\": ";
  dumpDwarfTypeIdent(CT);
  outs() << ",\n      \"is_decl\": " << (CT.isForwardDecl() ? "true" : "false");
  outs() << ",\n      \"is_odr\": " << (CT.getIdentifier().size() > 0 ? "true" : "false");

  unsigned tag = CT.getTag();
  const char *tag_name = nullptr;
  if (tag == dwarf::DW_TAG_structure_type) {
    tag_name = "DW_TAG_structure_type";
  } else if (tag == dwarf::DW_TAG_class_type) {
    tag_name = "DW_TAG_class_type";
  } else if (tag == dwarf::DW_TAG_union_type) {
    tag_name = "DW_TAG_union_type";
  }
  outs() << ",\n      \"tag\": " << CT.getTag();
  if (tag_name != nullptr) {
    outs() << ",\n      \"tag_name\": \"" << tag_name << "\"";
  }

  outs() << ",\n      \"elements\": [";
  bool first_element = true;
  for (DINode *E : CT.getElements()) {
    if (DIDerivedType *DIT = dyn_cast<DIDerivedType>(E)) {
      if (!first_element) {
        outs() << ",";
      } else {
        first_element = false;
      }

      unsigned tag = DIT->getTag();
      const char *tag_name = nullptr;
      if (tag == dwarf::DW_TAG_member) {
        tag_name = "DW_TAG_member";
      } else if (tag == dwarf::DW_TAG_inheritance) {
        tag_name = "DW_TAG_inheritance";
      }
      outs() << "\n        {";
      outs() << "\n          \"name\": ";
      dumpString(DIT->getName());
      outs() << ",\n          \"tag\": " << tag;
      if (tag_name != nullptr) {
        outs() << ",\n          \"tag_name\": \"" << tag_name << "\"";
      }
      outs() << ",\n          \"base_type\": ";
      DIType *Base = DIT->getBaseType();
      if (Base == nullptr) {
        outs() << "null";
      } else if (DICompositeType *BaseCT = dyn_cast<DICompositeType>(Base)) {
        dumpDwarfTypeIdent(*BaseCT);
      } else {
        dumpString(Base->getName());
      }
      outs() << ",\n          \"offset_bits\": " << DIT->getOffsetInBits();
      // This seems to be incorreclty 0 for DW_TAG_inheritance? So don't
      // output in this case (analyzer can recompute instead).
      if (DIT->getSizeInBits() != 0 || tag != dwarf::DW_TAG_inheritance) {
        outs() << ",\n          \"size_bits\": " << DIT->getSizeInBits();
      }

      if (DIT->getFlags() & DINode::FlagStaticMember) {
        outs() << ",\n          \"is_static\": true";
      }

      outs() << "\n        }";
    }
  }
  outs() << "\n      ]";

  outs() << "\n    }";
}

void dumpDwarfTypes(const Module &M) {
  std::vector<Metadata*> Queue;

  DenseSet<Metadata*> Seen;
  for (DICompileUnit *CU : M.debug_compile_units()) {
    Seen.insert(CU);
    Queue.push_back(CU);
  }
  for (const Function &F : M) {
    if (DISubprogram *SP = F.getSubprogram()) {
      Seen.insert(SP);
      Queue.push_back(SP);
    }
  }

  bool first = true;
  while (!Queue.empty()) {
    Metadata* Node = Queue.back();
    Queue.pop_back();
    if (MDNode *MD = dyn_cast<MDNode>(Node)) {
      if (DICompositeType *CT = dyn_cast<DICompositeType>(Node)) {
        if (CT->getTag() == dwarf::DW_TAG_array_type)
          continue;
        if (!first) {
          outs() << ',';
        } else {
          first = false;
        }
        outs() << '\n';

        dumpCompositeType(*CT);
      }
      for (Metadata *Op : MD->operands()) {
        if (Op == nullptr)
          continue;
        if (!Seen.insert(Op).second)
          continue;
        Queue.push_back(Op);
      }
    }
  }
}

PreservedAnalyses DumperPass::run(Module &M, ModuleAnalysisManager &MAM) {
  outs() << "{";
  outs() << "\n  \"source_filename\": \"" << M.getSourceFileName() << "\"";
  outs() << ",\n  \"dwarf_types\": [";
  dumpDwarfTypes(M);
  outs() << "\n  ]";

  outs() << ",\n  \"load_stores\": [";
  bool first = true;
  const DataLayout &DL = M.getDataLayout();
  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  for (Function &F : M) {
    if (F.empty())
      continue;
    BlockFrequencyInfo &BFI = FAM.getResult<BlockFrequencyAnalysis>(F);
    for (const BasicBlock &BB : F) {
      std::optional<uint64_t> BlockCount =
        BFI.getBlockProfileCount(&BB, /*AllowSynthetic=*/true);
      uint64_t ProfileCount = BlockCount.has_value() ? BlockCount.value() : 1.0;
      for (const Instruction &I : BB) {
        if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
          const char* kind;
          const Value* Ptr;
          Type* OpType;
          Align OpAlign;
          if (const LoadInst *LI = dyn_cast<LoadInst>(&I)) {
            kind = "load";
            Ptr = LI->getPointerOperand();
            OpType = LI->getType();
            OpAlign = LI->getAlign();
          } else {
            const StoreInst &SI = cast<StoreInst>(I);
            kind = "store";
            Ptr = SI.getPointerOperand();
            OpType = SI.getValueOperand()->getType();
            OpAlign = SI.getAlign();
          }

          if (!first) {
            outs() << ',';
          } else {
            first = false;
          }
          outs() << "\n    {";
          outs() << "\n      \"kind\": \"" << kind << "\"";
          outs() << ",\n      \"function\": ";
          dumpString(F.getName());
#if DUMP_IR
          outs() << ",\n      \"IR\": \"" << I << "\"";
#endif
          outs() << ",\n      \"profile_count\": " << ProfileCount;
          dumpAA(I);
          dumpOp(DL, OpType, OpAlign);
          dumpTypeGuess(DL, *Ptr);
          outs() << "\n    }";
        } else if (const IntrinsicInst *Intrin = dyn_cast<IntrinsicInst>(&I)) {
          Intrinsic::ID ID = Intrin->getIntrinsicID();
          unsigned ReadOp = ~0u;
          unsigned WriteOp = ~0u;
          unsigned SizeOp = ~0u;
          const char *Kind = nullptr;
          switch (ID) {
          case Intrinsic::memcpy:
          case Intrinsic::memcpy_inline:
          case Intrinsic::memcpy_element_unordered_atomic:
          case Intrinsic::memmove:
          case Intrinsic::memmove_element_unordered_atomic:
            Kind = "MemcpyLike";
            WriteOp = 0;
            ReadOp = 1;
            SizeOp = 2;
            break;
          case Intrinsic::memset:
          case Intrinsic::memset_inline:
          case Intrinsic::memset_element_unordered_atomic:
            Kind = "MemsetLike";
            WriteOp = 0;
            SizeOp = 2;
            break;
          default:
            break;
          }
          if (Kind == nullptr)
            continue;
          Value* SizeVal = Intrin->getOperand(SizeOp);
          if (!isa<ConstantInt>(SizeVal))
            continue;
          uint64_t Size = cast<ConstantInt>(SizeVal)->getZExtValue();
          if (ReadOp != ~0u) {
            if (!first) {
              outs() << ',';
            } else {
              first = false;
            }
            outs() << "\n    {";
            outs() << "\n      \"kind\": \"load\"";
            outs() << ",\n      \"ikind\": \"" << Kind << "\"";
            outs() << ",\n      \"function\": ";
            dumpString(F.getName());
#ifdef DUMP_IR
            outs() << ",\n      \"IR\": \"" << I << "\"";
#endif
            outs() << ",\n      \"profile_count\": " << ProfileCount;
            outs() << ",\n      \"size_bytes\": " << Size;
            dumpTypeGuess(DL, *Intrin->getOperand(ReadOp));
            outs() << "\n    }";
          }
          if (WriteOp != ~0u) {
            if (!first) {
              outs() << ',';
            } else {
              first = false;
            }
            outs() << "\n    {";
            outs() << "\n      \"kind\": \"store\"";
            outs() << ",\n      \"ikind\": \"" << Kind << "\"";
            outs() << ",\n      \"function\": ";
            dumpString(F.getName());
#ifdef DUMP_IR
            outs() << ",\n      \"IR\": \"" << I << "\"";
#endif
            outs() << ",\n      \"profile_count\": " << ProfileCount;
            outs() << ",\n      \"size_bytes\": " << Size;
            dumpTypeGuess(DL, *Intrin->getOperand(WriteOp));
            outs() << "\n    }";
          }
        }
      }
    }
  }
  outs() << "\n  ]";
  outs() << "\n}";
  outs() << '\n';
  return PreservedAnalyses::all();
}

}

int main(int argc, char **argv) {
  InitLLVM _(argc, argv);

  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeCore(Registry);
  initializeScalarOpts(Registry);
  initializeVectorization(Registry);
  initializeIPO(Registry);
  initializeAnalysis(Registry);
  initializeTransformUtils(Registry);
  initializeInstCombine(Registry);

  cl::ParseCommandLineOptions(argc, argv,
                              "dump memory access frequencies by struct\n");

  LLVMContext Ctx;

  std::unique_ptr<Module> M;

  SMDiagnostic Err;
  M = parseIRFile(InputFilename, Err, Ctx);
  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassInstrumentationCallbacks PIC;

  //StandardInstrumentations SI(Ctx, /*DebugLogging=*/false);
  //SI.registerCallbacks(PIC, &MAM);

  TargetMachine* TM = nullptr;
  PipelineTuningOptions PTO;
  std::optional<PGOOptions> P;
  PassBuilder PB(TM, PTO, P, &PIC);

  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
  MPM.addPass(DumperPass());

  MPM.run(*M, MAM);

  return 0;
}
