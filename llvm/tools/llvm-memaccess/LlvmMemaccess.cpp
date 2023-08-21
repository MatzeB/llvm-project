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

class Dumper : public PassInfoMixin<Dumper> {
public:
  Dumper() : OS(outs()) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  void dumpDwarfTypeIdent(const DICompositeType &CT);
  void dumpTypeGuess(const DataLayout &DL, const Value &Ptr);
  void dumpAA(const Instruction &I);
  void dumpOp(const DataLayout &DL, Type &OpType, Align A);
  void dumpCompositeType(const DICompositeType &CT);
  void dumpDwarfTypes(const Module &M);

  raw_ostream &OS;
};

static void dumpJSONEscaped(raw_ostream &OS, StringRef S) {
  size_t I = 0;
  for (;;) {
    size_t next_backslash = S.find('\\', I);
    size_t next_quote = S.find('"', I);
    size_t next_special = std::min(next_backslash, next_quote);
    if (next_special == StringRef::npos) {
      OS << S.substr(I);
      break;
    } else {
      OS << S.substr(I, next_special - I);
    }
    OS << '\\' << S[next_special];
    I = next_special + 1;
  }
}

static void dumpJSONString(raw_ostream &OS, StringRef S) {
  OS << '\"';
  dumpJSONEscaped(OS, S);
  OS << '\"';
}

struct Guess {
  const StructType *llvm_type = nullptr;
  const DICompositeType *dwarf_type = nullptr;
  APInt Offset;
};

static bool tryLLVMType(Guess *G, const llvm::Type *type) {
  const StructType *ST = dyn_cast<StructType>(type);
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

  // Perform a similar operation as GetElementPtr::accumulateConstantOffset
  // but for dynamic array indices we just choose 0 as a "representative
  // value".
  bool offset_valid = cast<GEPOperator>(GEP).accumulateConstantOffset(
      DL, Offset, [](Value &V, APInt &Offset) -> bool {
        Offset.clearAllBits();
        return true;
      });
  if (!offset_valid)
    return nullptr;

  const Type *PointeeType = GEP.getSourceElementType();
  if (tryLLVMType(G, PointeeType)) {
    G->Offset += Offset;
    return GEP.getPointerOperand();
  }
  return nullptr;
}

void guessAlloca(const DataLayout &DL, const AllocaInst &AI, Guess *G) {
  const Type *type = AI.getAllocatedType();
  tryLLVMType(G, type);
}

static const DIType *skipDerived(const DIType *Type) {
  const DIDerivedType *Derived = dyn_cast<DIDerivedType>(Type);
  if (Derived == nullptr)
    return Type;
  unsigned tag = Derived->getTag();
  if (tag == dwarf::DW_TAG_const_type ||
      tag == dwarf::DW_TAG_restrict_type ||
      tag == dwarf::DW_TAG_volatile_type ||
      tag == dwarf::DW_TAG_packed_type ||
      tag == dwarf::DW_TAG_atomic_type ||
      tag == dwarf::DW_TAG_immutable_type) {
    const DIType *Base = Derived->getBaseType();
    if (Base == nullptr)
      return Type;
    return skipDerived(Base);
  }
  return Type;
}

static const DIType *skipPointsTo(const DIType *Type) {
  const DIDerivedType *TypeD = dyn_cast<DIDerivedType>(skipDerived(Type));
  if (TypeD == nullptr)
    return nullptr;
  unsigned tag = TypeD->getTag();
  if (tag != dwarf::DW_TAG_pointer_type &&
      tag != dwarf::DW_TAG_reference_type &&
      tag != dwarf::DW_TAG_rvalue_reference_type)
    return nullptr;
  return TypeD->getBaseType();
}

static bool tryDwarfType(const DIType *Type, Guess *G) {
  const DIType *TypeS = skipDerived(Type);
  const DICompositeType *BaseC = dyn_cast<DICompositeType>(TypeS);
  if (BaseC == nullptr)
    return false;
  G->llvm_type = nullptr;
  G->dwarf_type = BaseC;
  return true;
}

static void guessCall(const CallBase &Call, Guess *G) {
  const Function *Callee = Call.getCalledFunction();
  if (Callee == nullptr)
    return;
  const DISubprogram *SP = Callee->getSubprogram();
  if (SP == nullptr)
    return;
  const DISubroutineType *Type = SP->getType();
  if (Type == nullptr)
    return;
  const DITypeRefArray Types = Type->getTypeArray();
  if (Types.size() == 0)
    return;
  const DIType *RetType = Types[0];
  if (const DIType *RetPointeeType = skipPointsTo(RetType)) {
    tryDwarfType(RetPointeeType, G);
  } else {
    tryDwarfType(RetType, G);
  }
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
    if (const DIType *PointeeType = skipPointsTo(Var->getType())) {
      tryDwarfType(PointeeType, G);
    }
  } else if (DbgVar->getIntrinsicID() == Intrinsic::dbg_declare) {
    DILocalVariable *Var = DbgVar->getVariable();
    tryDwarfType(Var->getType(), G);
  }
}

void Dumper::dumpDwarfTypeIdent(const DICompositeType &CT) {
  if (CT.getTag() == dwarf::DW_TAG_array_type) {
    // not really handled yet...
    OS << "\"$array\"";
    return;
  }
  StringRef OdrIdent = CT.getIdentifier();
  if (OdrIdent.size() > 0) {
    dumpJSONString(OS, OdrIdent);
    return;
  }
  // It's a local type.
  // hack: for now let's just assume there's no two types with the same name
  // defined on the same line...
  assert(CT.getLine() > 0);
  OS << "\"L.";
  dumpJSONEscaped(OS, CT.getName());
  OS << ':' << CT.getLine() << "\"";
}

void Dumper::dumpTypeGuess(const DataLayout &DL, const Value &Ptr) {
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
    OS << ",\n      \"trace_type\": ";
    dumpDwarfTypeIdent(*G.dwarf_type);
    OS << ",\n      \"trace_offset\": " << G.Offset;
  } else if (G.llvm_type != nullptr) {
    OS << ",\n      \"trace_type\": \"" << G.llvm_type->getStructName() << "\"";
    OS << ",\n      \"trace_offset\": " << G.Offset;
  }
}

void Dumper::dumpAA(const Instruction &I) {
  AAMDNodes AAMD = I.getAAMetadata();
  const MDNode *TBAA = AAMD.TBAA;
  if (TBAA == nullptr)
    return;
  const MDNode &BaseTy = cast<MDNode>(*TBAA->getOperand(0));
  const MDString &BaseTyName = cast<MDString>(*BaseTy.getOperand(0));
  OS << ",\n      \"aa_struct\": \"" << BaseTyName.getString() << "\"";
  const MDNode &AccessTy = cast<MDNode>(*TBAA->getOperand(1));
  const MDString &AccessTyName = cast<MDString>(*AccessTy.getOperand(0));
  OS << ",\n      \"aa_access_type\": \"" << AccessTyName.getString() << "\"";
  const ConstantAsMetadata &Offset = cast<ConstantAsMetadata>(*TBAA->getOperand(2));
  OS << ",\n      \"aa_offset\": "
     << cast<ConstantInt>(Offset.getValue())->getSExtValue();
}

void Dumper::dumpOp(const DataLayout &DL, Type &OpType, Align A) {
  TypeSize Sz = DL.getTypeStoreSize(&OpType);
  OS << ",\n      \"size_bytes\": " << Sz.getFixedValue();
  OS << ",\n      \"align_bytes\": " << A.value();
}

void Dumper::dumpCompositeType(const DICompositeType &CT) {
  OS << "    {";
  OS << "\n      \"name\": ";
  dumpJSONString(OS, CT.getName());
  OS << ",\n      \"file\": ";
  dumpJSONString(OS, CT.getFilename());
  OS << ",\n      \"line\": " << CT.getLine();
  OS << ",\n      \"ident\": ";
  dumpDwarfTypeIdent(CT);
  OS << ",\n      \"is_decl\": " << (CT.isForwardDecl() ? "true" : "false");
  OS << ",\n      \"is_odr\": "
     << (CT.getIdentifier().size() > 0 ? "true" : "false");

  if (CT.getSizeInBits() > 0) {
    OS << ",\n      \"size_bits\": " << CT.getSizeInBits();
  }

  unsigned tag = CT.getTag();
  const char *tag_name = nullptr;
  if (tag == dwarf::DW_TAG_structure_type) {
    tag_name = "DW_TAG_structure_type";
  } else if (tag == dwarf::DW_TAG_class_type) {
    tag_name = "DW_TAG_class_type";
  } else if (tag == dwarf::DW_TAG_union_type) {
    tag_name = "DW_TAG_union_type";
  } else if (tag == dwarf::DW_TAG_enumeration_type) {
    tag_name = "DW_TAG_enumeration_type";
  }
  OS << ",\n      \"tag\": " << CT.getTag();
  if (tag_name != nullptr) {
    OS << ",\n      \"tag_name\": \"" << tag_name << "\"";
  }

  OS << ",\n      \"elements\": [";
  bool first_element = true;
  for (DINode *E : CT.getElements()) {
    if (DIDerivedType *DIT = dyn_cast<DIDerivedType>(E)) {
      if (!first_element) {
        OS << ",";
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
      OS << "\n        {";
      OS << "\n          \"name\": ";
      dumpJSONString(OS, DIT->getName());
      OS << ",\n          \"tag\": " << tag;
      if (tag_name != nullptr) {
        OS << ",\n          \"tag_name\": \"" << tag_name << "\"";
      }
      OS << ",\n          \"base_type\": ";
      DIType *Base = DIT->getBaseType();
      if (Base == nullptr) {
        OS << "null";
      } else if (DICompositeType *BaseCT = dyn_cast<DICompositeType>(Base)) {
        dumpDwarfTypeIdent(*BaseCT);
      } else {
        dumpJSONString(OS, Base->getName());
      }
      OS << ",\n          \"offset_bits\": " << DIT->getOffsetInBits();
      // This seems to be incorreclty 0 for DW_TAG_inheritance? So don't
      // output in this case (analyzer can recompute instead).
      if (DIT->getSizeInBits() != 0 || tag != dwarf::DW_TAG_inheritance) {
        OS << ",\n          \"size_bits\": " << DIT->getSizeInBits();
      }

      if (DIT->getFlags() & DINode::FlagStaticMember) {
        OS << ",\n          \"is_static\": true";
      }

      OS << "\n        }";
    }
  }
  OS << "\n      ]";

  OS << "\n    }";
}

void Dumper::dumpDwarfTypes(const Module &M) {
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
          OS << ',';
        } else {
          first = false;
        }
        OS << '\n';

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

PreservedAnalyses Dumper::run(Module &M, ModuleAnalysisManager &MAM) {
  OS << "{";
  OS << "\n  \"source_filename\": \"" << M.getSourceFileName() << "\"";
  OS << ",\n  \"dwarf_types\": [";
  dumpDwarfTypes(M);
  OS << "\n  ]";

  OS << ",\n  \"load_stores\": [";
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
            OS << ',';
          } else {
            first = false;
          }
          OS << "\n    {";
          OS << "\n      \"kind\": \"" << kind << "\"";
          OS << ",\n      \"function\": ";
          dumpJSONString(OS, F.getName());
#if DUMP_IR
          OS << ",\n      \"IR\": \"" << I << "\"";
#endif
          OS << ",\n      \"profile_count\": " << ProfileCount;
          dumpAA(I);
          dumpOp(DL, *OpType, OpAlign);
          dumpTypeGuess(DL, *Ptr);
          OS << "\n    }";
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
              OS << ',';
            } else {
              first = false;
            }
            OS << "\n    {";
            OS << "\n      \"kind\": \"load\"";
            OS << ",\n      \"ikind\": \"" << Kind << "\"";
            OS << ",\n      \"function\": ";
            dumpJSONString(OS, F.getName());
#ifdef DUMP_IR
            OS << ",\n      \"IR\": \"" << I << "\"";
#endif
            OS << ",\n      \"profile_count\": " << ProfileCount;
            OS << ",\n      \"size_bytes\": " << Size;
            dumpTypeGuess(DL, *Intrin->getOperand(ReadOp));
            OS << "\n    }";
          }
          if (WriteOp != ~0u) {
            if (!first) {
              OS << ',';
            } else {
              first = false;
            }
            OS << "\n    {";
            OS << "\n      \"kind\": \"store\"";
            OS << ",\n      \"ikind\": \"" << Kind << "\"";
            OS << ",\n      \"function\": ";
            dumpJSONString(OS, F.getName());
#ifdef DUMP_IR
            OS << ",\n      \"IR\": \"" << I << "\"";
#endif
            OS << ",\n      \"profile_count\": " << ProfileCount;
            OS << ",\n      \"size_bytes\": " << Size;
            dumpTypeGuess(DL, *Intrin->getOperand(WriteOp));
            OS << "\n    }";
          }
        }
      }
    }
  }
  OS << "\n  ]";
  OS << "\n}";
  OS << '\n';
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
  MPM.addPass(Dumper());

  MPM.run(*M, MAM);

  return 0;
}
