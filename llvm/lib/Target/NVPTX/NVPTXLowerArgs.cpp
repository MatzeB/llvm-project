//===-- NVPTXLowerArgs.cpp - Lower arguments ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
// Arguments to kernel and device functions are passed via param space,
// which imposes certain restrictions:
// http://docs.nvidia.com/cuda/parallel-thread-execution/#state-spaces
//
// Kernel parameters are read-only and accessible only via ld.param
// instruction, directly or via a pointer. Pointers to kernel
// arguments can't be converted to generic address space.
//
// Device function parameters are directly accessible via
// ld.param/st.param, but taking the address of one returns a pointer
// to a copy created in local space which *can't* be used with
// ld.param/st.param.
//
// Copying a byval struct into local memory in IR allows us to enforce
// the param space restrictions, gives the rest of IR a pointer w/o
// param space restrictions, and gives us an opportunity to eliminate
// the copy.
//
// facebook begin T64625594
// nvptx-max-arg-size-to-lower can be used to set maximum size of
// a byval struct to be copiped into local memory, if possible.
// facebook end T64625594
//
// Pointer arguments to kernel functions need more work to be lowered:
//
// 1. Convert non-byval pointer arguments of CUDA kernels to pointers in the
//    global address space. This allows later optimizations to emit
//    ld.global.*/st.global.* for accessing these pointer arguments. For
//    example,
//
//    define void @foo(float* %input) {
//      %v = load float, float* %input, align 4
//      ...
//    }
//
//    becomes
//
//    define void @foo(float* %input) {
//      %input2 = addrspacecast float* %input to float addrspace(1)*
//      %input3 = addrspacecast float addrspace(1)* %input2 to float*
//      %v = load float, float* %input3, align 4
//      ...
//    }
//
//    Later, NVPTXInferAddressSpaces will optimize it to
//
//    define void @foo(float* %input) {
//      %input2 = addrspacecast float* %input to float addrspace(1)*
//      %v = load float, float addrspace(1)* %input2, align 4
//      ...
//    }
//
// 2. Convert pointers in a byval kernel parameter to pointers in the global
//    address space. As #2, it allows NVPTX to emit more ld/st.global. E.g.,
//
//    struct S {
//      int *x;
//      int *y;
//    };
//    __global__ void foo(S s) {
//      int *b = s.y;
//      // use b
//    }
//
//    "b" points to the global address space. In the IR level,
//
//    define void @foo({i32*, i32*}* byval %input) {
//      %b_ptr = getelementptr {i32*, i32*}, {i32*, i32*}* %input, i64 0, i32 1
//      %b = load i32*, i32** %b_ptr
//      ; use %b
//    }
//
//    becomes
//
//    define void @foo({i32*, i32*}* byval %input) {
//      %b_ptr = getelementptr {i32*, i32*}, {i32*, i32*}* %input, i64 0, i32 1
//      %b = load i32*, i32** %b_ptr
//      %b_global = addrspacecast i32* %b to i32 addrspace(1)*
//      %b_generic = addrspacecast i32 addrspace(1)* %b_global to i32*
//      ; use %b_generic
//    }
//
// TODO: merge this pass with NVPTXInferAddressSpaces so that other passes don't
// cancel the addrspacecast pair this pass emits.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "NVPTX.h"
#include "NVPTXTargetMachine.h"
#include "NVPTXUtilities.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include <numeric>
#include <queue>
// facebook begin T64625594
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/CommandLine.h"
// facebook end

#define DEBUG_TYPE "nvptx-lower-args"

using namespace llvm;

// facebook begin T64625594
#define DEBUG_TYPE "nvptx-lower-pointer-args"

static cl::opt<unsigned> NVPTXMaxArgSizeToLower(
    "nvptx-max-arg-size-to-lower", cl::init(0), cl::Hidden,
    cl::desc("Maximum size in bytes of a kernel argument to lower."));
// facebook end

namespace llvm {
void initializeNVPTXLowerArgsPass(PassRegistry &);
}

namespace {
class NVPTXLowerArgs : public FunctionPass {
  bool runOnFunction(Function &F) override;

  bool runOnKernelFunction(Function &F);
  bool runOnDeviceFunction(Function &F);

  // handle byval parameters
  void handleByValParam(Argument *Arg);
  // Knowing Ptr must point to the global address space, this function
  // addrspacecasts Ptr to global and then back to generic. This allows
  // NVPTXInferAddressSpaces to fold the global-to-generic cast into
  // loads/stores that appear later.
  void markPointerAsGlobal(Value *Ptr);

  // facebook begin T64625594
  // Returns a set of kernel arguments that are used in instructions that
  // require a non-param address space.
  void collectArgsWithNonParamUses(
      Function &F, llvm::SmallDenseSet<Argument *> *ArgsInNonParamUse) const;

  // Add addrspacecast to param and then back to generic.
  // Expect InferAddressSpaces to fold the param-to-generic casts.
  void markArgPointerAsParam(Argument *Arg);
  // facebook end

public:
  static char ID; // Pass identification, replacement for typeid
  NVPTXLowerArgs(const NVPTXTargetMachine *TM = nullptr)
      : FunctionPass(ID), TM(TM) {}
  StringRef getPassName() const override {
    return "Lower pointer arguments of CUDA kernels";
  }

private:
  const NVPTXTargetMachine *TM;
};
} // namespace

char NVPTXLowerArgs::ID = 1;

INITIALIZE_PASS(NVPTXLowerArgs, "nvptx-lower-args",
                "Lower arguments (NVPTX)", false, false)

// =============================================================================
// If the function had a byval struct ptr arg, say foo(%struct.x* byval %d),
// and we can't guarantee that the only accesses are loads,
// then add the following instructions to the first basic block:
//
// %temp = alloca %struct.x, align 8
// %tempd = addrspacecast %struct.x* %d to %struct.x addrspace(101)*
// %tv = load %struct.x addrspace(101)* %tempd
// store %struct.x %tv, %struct.x* %temp, align 8
//
// The above code allocates some space in the stack and copies the incoming
// struct from param space to local space.
// Then replace all occurrences of %d by %temp.
//
// In case we know that all users are GEPs or Loads, replace them with the same
// ones in parameter AS, so we can access them using ld.param.
// =============================================================================

// Replaces the \p OldUser instruction with the same in parameter AS.
// Only Load and GEP are supported.
static void convertToParamAS(Value *OldUser, Value *Param) {
  Instruction *I = dyn_cast<Instruction>(OldUser);
  assert(I && "OldUser must be an instruction");
  struct IP {
    Instruction *OldInstruction;
    Value *NewParam;
  };
  SmallVector<IP> ItemsToConvert = {{I, Param}};
  SmallVector<Instruction *> InstructionsToDelete;

  auto CloneInstInParamAS = [](const IP &I) -> Value * {
    if (auto *LI = dyn_cast<LoadInst>(I.OldInstruction)) {
      LI->setOperand(0, I.NewParam);
      return LI;
    }
    if (auto *GEP = dyn_cast<GetElementPtrInst>(I.OldInstruction)) {
      SmallVector<Value *, 4> Indices(GEP->indices());
      auto *NewGEP = GetElementPtrInst::Create(GEP->getSourceElementType(),
                                               I.NewParam, Indices,
                                               GEP->getName(), GEP);
      NewGEP->setIsInBounds(GEP->isInBounds());
      return NewGEP;
    }
    if (auto *BC = dyn_cast<BitCastInst>(I.OldInstruction)) {
      auto *NewBCType = PointerType::getWithSamePointeeType(
          cast<PointerType>(BC->getType()), ADDRESS_SPACE_PARAM);
      return BitCastInst::Create(BC->getOpcode(), I.NewParam, NewBCType,
                                 BC->getName(), BC);
    }
    if (auto *ASC = dyn_cast<AddrSpaceCastInst>(I.OldInstruction)) {
      assert(ASC->getDestAddressSpace() == ADDRESS_SPACE_PARAM);
      (void)ASC;
      // Just pass through the argument, the old ASC is no longer needed.
      return I.NewParam;
    }
    llvm_unreachable("Unsupported instruction");
  };

  while (!ItemsToConvert.empty()) {
    IP I = ItemsToConvert.pop_back_val();
    Value *NewInst = CloneInstInParamAS(I);

    if (NewInst && NewInst != I.OldInstruction) {
      // We've created a new instruction. Queue users of the old instruction to
      // be converted and the instruction itself to be deleted. We can't delete
      // the old instruction yet, because it's still in use by a load somewhere.
      for (Value *V : I.OldInstruction->users())
        ItemsToConvert.push_back({cast<Instruction>(V), NewInst});

      InstructionsToDelete.push_back(I.OldInstruction);
    }
  }

  // Now we know that all argument loads are using addresses in parameter space
  // and we can finally remove the old instructions in generic AS.  Instructions
  // scheduled for removal should be processed in reverse order so the ones
  // closest to the load are deleted first. Otherwise they may still be in use.
  // E.g if we have Value = Load(BitCast(GEP(arg))), InstructionsToDelete will
  // have {GEP,BitCast}. GEP can't be deleted first, because it's still used by
  // the BitCast.
  for (Instruction *I : llvm::reverse(InstructionsToDelete))
    I->eraseFromParent();
}

// Adjust alignment of arguments passed byval in .param address space. We can
// increase alignment of such arguments in a way that ensures that we can
// effectively vectorize their loads. We should also traverse all loads from
// byval pointer and adjust their alignment, if those were using known offset.
// Such alignment changes must be conformed with parameter store and load in
// NVPTXTargetLowering::LowerCall.
static void adjustByValArgAlignment(Argument *Arg, Value *ArgInParamAS,
                                    const NVPTXTargetLowering *TLI) {
  Function *Func = Arg->getParent();
  Type *StructType = Arg->getParamByValType();
  const DataLayout DL(Func->getParent());

  uint64_t NewArgAlign =
      TLI->getFunctionParamOptimizedAlign(Func, StructType, DL).value();
  uint64_t CurArgAlign =
      Arg->getAttribute(Attribute::Alignment).getValueAsInt();

  if (CurArgAlign >= NewArgAlign)
    return;

  LLVM_DEBUG(dbgs() << "Try to use alignment " << NewArgAlign << " instead of "
                    << CurArgAlign << " for " << *Arg << '\n');

  auto NewAlignAttr =
      Attribute::get(Func->getContext(), Attribute::Alignment, NewArgAlign);
  Arg->removeAttr(Attribute::Alignment);
  Arg->addAttr(NewAlignAttr);

  struct Load {
    LoadInst *Inst;
    uint64_t Offset;
  };

  struct LoadContext {
    Value *InitialVal;
    uint64_t Offset;
  };

  SmallVector<Load> Loads;
  std::queue<LoadContext> Worklist;
  Worklist.push({ArgInParamAS, 0});

  while (!Worklist.empty()) {
    LoadContext Ctx = Worklist.front();
    Worklist.pop();

    for (User *CurUser : Ctx.InitialVal->users()) {
      if (auto *I = dyn_cast<LoadInst>(CurUser)) {
        Loads.push_back({I, Ctx.Offset});
        continue;
      }

      if (auto *I = dyn_cast<BitCastInst>(CurUser)) {
        Worklist.push({I, Ctx.Offset});
        continue;
      }

      if (auto *I = dyn_cast<GetElementPtrInst>(CurUser)) {
        APInt OffsetAccumulated =
            APInt::getZero(DL.getIndexSizeInBits(ADDRESS_SPACE_PARAM));

        if (!I->accumulateConstantOffset(DL, OffsetAccumulated))
          continue;

        uint64_t OffsetLimit = -1;
        uint64_t Offset = OffsetAccumulated.getLimitedValue(OffsetLimit);
        assert(Offset != OffsetLimit && "Expect Offset less than UINT64_MAX");

        Worklist.push({I, Ctx.Offset + Offset});
        continue;
      }

      llvm_unreachable("All users must be one of: load, "
                       "bitcast, getelementptr.");
    }
  }

  for (Load &CurLoad : Loads) {
    Align NewLoadAlign(std::gcd(NewArgAlign, CurLoad.Offset));
    Align CurLoadAlign(CurLoad.Inst->getAlign());
    CurLoad.Inst->setAlignment(std::max(NewLoadAlign, CurLoadAlign));
  }
}

void NVPTXLowerArgs::handleByValParam(Argument *Arg) {
  Function *Func = Arg->getParent();
  Instruction *FirstInst = &(Func->getEntryBlock().front());
  Type *StructType = Arg->getParamByValType();
  assert(StructType && "Missing byval type");

  auto IsALoadChain = [&](Value *Start) {
    SmallVector<Value *, 16> ValuesToCheck = {Start};
    auto IsALoadChainInstr = [](Value *V) -> bool {
      if (isa<GetElementPtrInst>(V) || isa<BitCastInst>(V) || isa<LoadInst>(V))
        return true;
      // ASC to param space are OK, too -- we'll just strip them.
      if (auto *ASC = dyn_cast<AddrSpaceCastInst>(V)) {
        if (ASC->getDestAddressSpace() == ADDRESS_SPACE_PARAM)
          return true;
      }
      return false;
    };

    while (!ValuesToCheck.empty()) {
      Value *V = ValuesToCheck.pop_back_val();
      if (!IsALoadChainInstr(V)) {
        LLVM_DEBUG(dbgs() << "Need a copy of " << *Arg << " because of " << *V
                          << "\n");
        (void)Arg;
        return false;
      }
      if (!isa<LoadInst>(V))
        llvm::append_range(ValuesToCheck, V->users());
    }
    return true;
  };

  if (llvm::all_of(Arg->users(), IsALoadChain)) {
    // Convert all loads and intermediate operations to use parameter AS and
    // skip creation of a local copy of the argument.
    SmallVector<User *, 16> UsersToUpdate(Arg->users());
    Value *ArgInParamAS = new AddrSpaceCastInst(
        Arg, PointerType::get(StructType, ADDRESS_SPACE_PARAM), Arg->getName(),
        FirstInst);
    for (Value *V : UsersToUpdate)
      convertToParamAS(V, ArgInParamAS);
    LLVM_DEBUG(dbgs() << "No need to copy " << *Arg << "\n");

    // Further optimizations require target lowering info.
    if (!TM)
      return;

    const auto *TLI =
        cast<NVPTXTargetLowering>(TM->getSubtargetImpl()->getTargetLowering());

    adjustByValArgAlignment(Arg, ArgInParamAS, TLI);

    return;
  }

  // Otherwise we have to create a temporary copy.
  const DataLayout &DL = Func->getParent()->getDataLayout();
  unsigned AS = DL.getAllocaAddrSpace();
  AllocaInst *AllocA = new AllocaInst(StructType, AS, Arg->getName(), FirstInst);
  // Set the alignment to alignment of the byval parameter. This is because,
  // later load/stores assume that alignment, and we are going to replace
  // the use of the byval parameter with this alloca instruction.
  AllocA->setAlignment(Func->getParamAlign(Arg->getArgNo())
                           .value_or(DL.getPrefTypeAlign(StructType)));
  Arg->replaceAllUsesWith(AllocA);

  Value *ArgInParam = new AddrSpaceCastInst(
      Arg, PointerType::get(StructType, ADDRESS_SPACE_PARAM), Arg->getName(),
      FirstInst);
  // Be sure to propagate alignment to this load; LLVM doesn't know that NVPTX
  // addrspacecast preserves alignment.  Since params are constant, this load is
  // definitely not volatile.
  LoadInst *LI =
      new LoadInst(StructType, ArgInParam, Arg->getName(),
                   /*isVolatile=*/false, AllocA->getAlign(), FirstInst);
  new StoreInst(LI, AllocA, FirstInst);
}

void NVPTXLowerArgs::markPointerAsGlobal(Value *Ptr) {
  if (Ptr->getType()->getPointerAddressSpace() == ADDRESS_SPACE_GLOBAL)
    return;

  // Deciding where to emit the addrspacecast pair.
  BasicBlock::iterator InsertPt;
  if (Argument *Arg = dyn_cast<Argument>(Ptr)) {
    // Insert at the functon entry if Ptr is an argument.
    InsertPt = Arg->getParent()->getEntryBlock().begin();
  } else {
    // Insert right after Ptr if Ptr is an instruction.
    InsertPt = ++cast<Instruction>(Ptr)->getIterator();
    assert(InsertPt != InsertPt->getParent()->end() &&
           "We don't call this function with Ptr being a terminator.");
  }

  Instruction *PtrInGlobal = new AddrSpaceCastInst(
      Ptr,
      PointerType::getWithSamePointeeType(cast<PointerType>(Ptr->getType()),
                                          ADDRESS_SPACE_GLOBAL),
      Ptr->getName(), &*InsertPt);
  Value *PtrInGeneric = new AddrSpaceCastInst(PtrInGlobal, Ptr->getType(),
                                              Ptr->getName(), &*InsertPt);
  // Replace with PtrInGeneric all uses of Ptr except PtrInGlobal.
  Ptr->replaceAllUsesWith(PtrInGeneric);
  PtrInGlobal->setOperand(0, Ptr);
}

// facebook begin T64625594
void NVPTXLowerArgs::collectArgsWithNonParamUses(
    Function &F, llvm::SmallDenseSet<Argument *> *ArgsInNonParamUse) const {
  auto InsertIfArg = [&](Value *V) {
    Value *U = getUnderlyingObject(V);
    if (Argument *Arg = dyn_cast<Argument>(U)) {
      ArgsInNonParamUse->insert(Arg);
    }
  };

  for (auto &B : F) {
    for (auto &I : B) {
      switch (I.getOpcode()) {
      default: {
        assert(!I.mayWriteToMemory());
        break;
      }
      case Instruction::Store: {
        InsertIfArg(I.getOperand(1));
        break;
      }
      case Instruction::Select: {
        if (I.getType()->isPointerTy()) {
          InsertIfArg(I.getOperand(1));
          InsertIfArg(I.getOperand(2));
        }
        break;
      }
      case Instruction::PHI: {
        if (I.getType()->isPointerTy()) {
          const PHINode *PN = cast<PHINode>(&I);
          for (Value *V : PN->incoming_values()) {
            InsertIfArg(V);
          }
        }
        break;
      }
      case Instruction::Call:
      case Instruction::CallBr:
      case Instruction::Invoke:
        const CallBase *CB = cast<CallBase>(&I);
        for (Value *V : CB->args()) {
          InsertIfArg(V);
        }
        break;
      }
    }
  }
}

void NVPTXLowerArgs::markArgPointerAsParam(Argument *Arg) {
  if (Arg->getType()->getPointerAddressSpace() == ADDRESS_SPACE_PARAM)
    return;

  Instruction *FirstInst = &(Arg->getParent()->getEntryBlock().front());

  Instruction *PtrInParam = new AddrSpaceCastInst(
      Arg,
      PointerType::get(Arg->getType()->getPointerElementType(),
                       ADDRESS_SPACE_PARAM),
      Arg->getName(), FirstInst);
  Value *PtrInGeneric = new AddrSpaceCastInst(PtrInParam, Arg->getType(),
                                              Arg->getName(), FirstInst);

  // Replace with PtrInGeneric all uses of Arg except PtrInParam.
  Arg->replaceAllUsesWith(PtrInGeneric);
  PtrInParam->setOperand(0, Arg);
}
// facebook end

// =============================================================================
// Main function for this pass.
// =============================================================================
bool NVPTXLowerArgs::runOnKernelFunction(Function &F) {
  // facebook begin T64625594
  const DataLayout &DL = F.getParent()->getDataLayout();

  if (TM && TM->getDrvInterface() == NVPTX::CUDA) {
    // Mark pointers in byval structs as global.
    for (auto &B : F) {
      for (auto &I : B) {
        if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
          if (LI->getType()->isPointerTy()) {
            Value *UO = getUnderlyingObject(LI->getPointerOperand());
            if (Argument *Arg = dyn_cast<Argument>(UO)) {
              if (Arg->hasByValAttr()) {
                // LI is a load from a pointer within a byval kernel parameter.
                markPointerAsGlobal(LI);
              }
            }
          }
        }
      }
    }
  }

  LLVM_DEBUG(dbgs() << "Lowering kernel args of " << F.getName() << "\n");
  // InferAddressSpaces is enabled when getOptLevel() != None.
  // Do not respect NVPTXMaxArgSizeToLower if getOptLevel() == None. Because we
  // rely on InferAddressSpaces to clean up the illegal param to generic
  // addspacecast that markArgPointerAsParam adds.
  // Assuming that TM is nullptr when this runs via opt for tests.
  if (NVPTXMaxArgSizeToLower > 0 &&
      (!TM || TM->getOptLevel() != CodeGenOpt::None)) {
    llvm::SmallDenseSet<Argument *> ArgsInNonParamUse;
    collectArgsWithNonParamUses(F, &ArgsInNonParamUse);
    for (Argument &Arg : F.args()) {
      if (Arg.getType()->isPointerTy() && Arg.hasByValAttr()) {
        auto ArgSize =
            DL.getTypeStoreSize(Arg.getType()->getPointerElementType());

        if (ArgSize < NVPTXMaxArgSizeToLower ||
            ArgsInNonParamUse.count(&Arg) != 0)
          handleByValParam(&Arg);
        else
          markArgPointerAsParam(&Arg);
      }
    }
  } else {
    for (Argument &Arg : F.args()) {
      if (Arg.getType()->isPointerTy() && Arg.hasByValAttr()) {
        handleByValParam(&Arg);
      }
    }
  }

  if (TM && TM->getDrvInterface() == NVPTX::CUDA) {
    // Mark pointer non-byval Args as global.
    for (Argument &Arg : F.args()) {
      if (Arg.getType()->isPointerTy() && !Arg.hasByValAttr()) {
        markPointerAsGlobal(&Arg);
      }
    }
  }
  // facebook end
  return true;
}

// Device functions only need to copy byval args into local memory.
bool NVPTXLowerArgs::runOnDeviceFunction(Function &F) {
  LLVM_DEBUG(dbgs() << "Lowering function args of " << F.getName() << "\n");
  for (Argument &Arg : F.args())
    if (Arg.getType()->isPointerTy() && Arg.hasByValAttr())
      handleByValParam(&Arg);
  return true;
}

bool NVPTXLowerArgs::runOnFunction(Function &F) {
  return isKernelFunction(F) ? runOnKernelFunction(F) : runOnDeviceFunction(F);
}

FunctionPass *
llvm::createNVPTXLowerArgsPass(const NVPTXTargetMachine *TM) {
  return new NVPTXLowerArgs(TM);
}
