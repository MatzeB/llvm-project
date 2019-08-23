// facebook T48837209
//===- X86BlockAnnotationInserter.cpp - Insert block info into binary -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a pass that inserts block label before its first
// instruction as an inline asm. Once binary is generated the label can be found
// from the symbol table with its address and provides a link between the binary
// and MIR level block information. A lable of each block is a concatenation of
// function GUID, block ID, and the profile count of the block (if available),
// with "__BLI" prefix.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86TargetMachine.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/LazyMachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineSizeOpts.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/FileSystem.h"
#include <unordered_set>

using namespace llvm;

namespace llvm {
extern cl::opt<int> ProfileSummaryCutoffHot;

static cl::opt<bool> AnnotateHotFuncsOnly(
    "block-annotate-hot-funcs-only", cl::init(true), cl::Hidden,
    cl::desc("Do block counts annotation for hot functions only"));
} // namespace llvm

namespace {
class X86BlockAnnotationInserter : public MachineFunctionPass {
  std::unordered_set<std::string> VisitedModules;

public:
  static char ID;

  X86BlockAnnotationInserter() : MachineFunctionPass(ID) {
    initializeX86BlockAnnotationInserterPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "X86 Block Label Inserter"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<LazyMachineBlockFrequencyInfoPass>();
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Like ProfileSummaryInfo::isHotBlockNthPercentile but for
  /// MachineBasicBlock.
  static bool isHotBlockNthPercentile(int PercentileCutoff,
                                      const MachineBasicBlock *MBB,
                                      ProfileSummaryInfo *PSI,
                                      const MachineBlockFrequencyInfo *MBFI) {
    auto Count = MBFI->getBlockProfileCount(MBB);
    return Count && PSI->isHotCountNthPercentile(PercentileCutoff, *Count);
  }

  /// Like ProfileSummaryInfo::isFunctionHotInCallGraphNthPercentile but for
  /// MachineFunction.
  static bool isFunctionHotInCallGraphNthPercentile(
      int PercentileCutoff, const MachineFunction *MF, ProfileSummaryInfo *PSI,
      const MachineBlockFrequencyInfo &MBFI) {
    if (auto FunctionCount = MF->getFunction().getEntryCount())
      if (PSI->isHotCountNthPercentile(PercentileCutoff,
                                       FunctionCount->getCount()))
        return true;
    for (const auto &MBB : *MF)
      if (isHotBlockNthPercentile(PercentileCutoff, &MBB, PSI, &MBFI))
        return true;
    return false;
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    GlobalValue::GUID GUID = MF.getFunction().getGUID();
    const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
    MachineBlockFrequencyInfo &MBFI =
        getAnalysis<LazyMachineBlockFrequencyInfoPass>().getBFI();
    ProfileSummaryInfo *PSI =
        &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();

    if (AnnotateHotFuncsOnly && PSI && PSI->hasProfileSummary())
      if (!isFunctionHotInCallGraphNthPercentile(ProfileSummaryCutoffHot, &MF,
                                                 PSI, MBFI))
        return false;

    bool Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      std::string ProfileCount;
      if (std::optional<uint64_t> PC = MBFI.getBlockProfileCount(&MBB))
        ProfileCount = Twine(PC.value()).str();
      else
        ProfileCount = "_";

      // Insert inline asm. Flag settings are copied from the machine
      // instruction generated from the actual inline asm with label.
      DebugLoc DL;
      std::string Label = "__BLI." + Twine(GUID).str() + "." +
                          Twine(MBB.getNumber()).str() + "." + ProfileCount +
                          ":";
      unsigned ExtraInfo = InlineAsm::Extra_HasSideEffects | InlineAsm::AD_ATT;
      unsigned Flag = InlineAsm::getFlagWord(InlineAsm::Kind_Clobber, 1);
      BuildMI(MBB, MBB.begin(), DL, TII->get(TargetOpcode::INLINEASM))
          .addExternalSymbol(MF.createExternalSymbolName(Label))
          .addImm(ExtraInfo)
          .addImm(Flag)
          .addReg(X86::DF,
                  RegState::Define | RegState::EarlyClobber |
                      getImplRegState(Register::isPhysicalRegister(X86::DF)))
          .addImm(Flag)
          .addReg(X86::FPSW,
                  RegState::Define | RegState::EarlyClobber |
                      getImplRegState(Register::isPhysicalRegister(X86::FPSW)))
          .addImm(Flag)
          .addReg(X86::EFLAGS, RegState::Define | RegState::EarlyClobber |
                                   getImplRegState(Register::isPhysicalRegister(
                                       X86::EFLAGS)));
      Changed = true;
    }

    return Changed;
  }
};
} // namespace

char X86BlockAnnotationInserter::ID = 0;
INITIALIZE_PASS_BEGIN(X86BlockAnnotationInserter, "block-annotation-inserter",
                      "Encode basic block info as inline asm.", false, false)
INITIALIZE_PASS_DEPENDENCY(LazyMachineBlockFrequencyInfoPass)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_END(X86BlockAnnotationInserter, "block-annotation-inserter",
                    "Encode basic block info as inline asm.", false, false)
FunctionPass *llvm::createX86BlockAnnotationInserter() {
  return new X86BlockAnnotationInserter();
}
