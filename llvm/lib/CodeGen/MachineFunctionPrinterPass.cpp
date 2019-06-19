//===-- MachineFunctionPrinterPass.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MachineFunctionPrinterPass implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LazyMachineBlockFrequencyInfo.h" // facebook T46037538
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h" // facebook T46037538
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// facebook begin T46037538
// Command line option to control printing of the block profile count
extern bool PrintProfAny();
extern cl::opt<bool> PrintForDev;
// facebook end

namespace {
/// MachineFunctionPrinterPass - This is a pass to dump the IR of a
/// MachineFunction.
///
struct MachineFunctionPrinterPass : public MachineFunctionPass {
  static char ID;

  raw_ostream &OS;
  const std::string Banner;

  MachineFunctionPrinterPass() : MachineFunctionPass(ID), OS(dbgs()) { }
  MachineFunctionPrinterPass(raw_ostream &os, const std::string &banner)
      : MachineFunctionPass(ID), OS(os), Banner(banner) {}

  StringRef getPassName() const override { return "MachineFunction Printer"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addUsedIfAvailable<SlotIndexes>();
    AU.addRequired<LazyMachineBlockFrequencyInfoPass>(); // facebook T46037538
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!isFunctionInPrintList(MF.getName()))
      return false;
    OS << "# " << Banner << ":\n";
    // facebook begin T46037538
    MachineBlockFrequencyInfo *MBFI =
        PrintProfAny() || PrintForDev
            ? &(getAnalysis<LazyMachineBlockFrequencyInfoPass>().getBFI())
            : nullptr;
    MF.print(OS, getAnalysisIfAvailable<SlotIndexes>(), MBFI);
    // facebook end
    return false;
  }
};

char MachineFunctionPrinterPass::ID = 0;
}

char &llvm::MachineFunctionPrinterPassID = MachineFunctionPrinterPass::ID;
// facebook begin T46037538
INITIALIZE_PASS_BEGIN(MachineFunctionPrinterPass, "machineinstr-printer",
                      "Machine Function Printer", false, false)
INITIALIZE_PASS_DEPENDENCY(LazyMachineBlockFrequencyInfoPass)
INITIALIZE_PASS_END(MachineFunctionPrinterPass, "machineinstr-printer",
                    "Machine Function Printer", false, false)
// facebook end

namespace llvm {
/// Returns a newly-created MachineFunction Printer pass. The
/// default banner is empty.
///
MachineFunctionPass *createMachineFunctionPrinterPass(raw_ostream &OS,
                                                      const std::string &Banner){
  return new MachineFunctionPrinterPass(OS, Banner);
}

}
