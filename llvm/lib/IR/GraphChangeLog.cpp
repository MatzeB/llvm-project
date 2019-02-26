//===- GraphChangeLog.cpp - Detailed phase changes to IR --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the implementation for the GraphChangeLog class
/// which records detailed IR changes by phase for comparing different compiler
/// or option variants.
///
/// facebook T13480588
///
//===----------------------------------------------------------------------===//

#include "llvm/IR/GraphChangeLog.h"

#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <unordered_map>
#include <vector>

using namespace llvm;

static cl::opt<std::string> FunctionToLog("log-changes-func", cl::Hidden);

namespace {

bool FinalPassSeen = false;

// Record the list of passes executed.
struct LogPassInfo {
  unsigned Instance;
  const char *Name;
};
std::vector<LogPassInfo> Passes;

unsigned BlockNumber = 0;
std::unordered_map<std::string, unsigned> PassInstance;
struct BlockInfo {
  // Unique small number set when block first encountered.
  unsigned Index;
  // Seen in recent pass, 0 means not previously in the graph.
  unsigned PassNumber;
  BlockInfo() : PassNumber(0) {}
};
unsigned InstructionNumber = 0;

std::unordered_map<BasicBlock *, BlockInfo> Blocks;
struct InstructionInfo {
  // Unique small number set when instruction first encountered.
  unsigned Index;
  // Most recent block which is the parent of the instruction.
  unsigned BlockNumber;
  // Seen in recent pass, 0 means not previously in the graph.
  unsigned PassNumber;
  InstructionInfo() : PassNumber(0) {}
};
std::unordered_map<Instruction *, InstructionInfo> Instructions;

// Analyze changes to the IR of \p F after execution of
// a pass with name \p PassName.
void afterPass(Function &F, const char *PassName, raw_ostream &OS) {
  unsigned &Instance = PassInstance[std::string(PassName)];
  Instance++;
  Passes.push_back({Instance, PassName});
  unsigned PassNumber = Passes.size();

  OS << "ChangeLog: in " << PassName << '\n';
  for (auto &BB : F) {
    auto &BBInfo = Blocks[&BB];
    if (BBInfo.PassNumber == 0) {
      BBInfo.Index = BlockNumber++;
      OS << "ChangeLog: block new " << BBInfo.Index << ' ' << BB.getName()
         << '\n';
    }
    BBInfo.PassNumber = PassNumber;
    for (auto &I : BB) {
      auto &IInfo = Instructions[&I];
      if (IInfo.PassNumber == 0) {
        IInfo.Index = InstructionNumber++;
        IInfo.BlockNumber = BBInfo.Index;
        OS << "ChangeLog: inst new " << IInfo.Index << ' ' << I << '\n';
      }
      IInfo.PassNumber = PassNumber;
      if (IInfo.BlockNumber != BBInfo.Index) {
        OS << "ChangeLog: inst moved " << IInfo.Index << ' '
           << IInfo.BlockNumber << "->" << BBInfo.Index << ' ' << I << '\n';
        IInfo.BlockNumber = BBInfo.Index;
      }
    }
  }
  std::vector<unsigned> Deleted;
  for (auto &Entry : Blocks) {
    if (Entry.second.PassNumber && Entry.second.PassNumber != PassNumber) {
      Deleted.push_back(Entry.second.Index);
      Entry.second.PassNumber = 0;
    }
  }
  std::sort(Deleted.begin(), Deleted.end());
  for (auto Index : Deleted)
    OS << "ChangeLog: block deleted " << Index << '\n';

  Deleted.clear();
  for (auto &Entry : Instructions) {
    if (Entry.second.PassNumber && Entry.second.PassNumber != PassNumber) {
      Deleted.push_back(Entry.second.Index);
      Entry.second.PassNumber = 0;
    }
  }
  std::sort(Deleted.begin(), Deleted.end());
  for (auto Index : Deleted)
    OS << "ChangeLog: inst deleted " << Index << '\n';
}

void finalPass(Function &F, raw_ostream &OS) {
  OS << "ChangeLog: in final\n";
  for (auto &BB : F) {
    auto &BBInfo = Blocks[&BB];
    if (BBInfo.PassNumber == 0) {
      BBInfo.Index = BlockNumber++;
      OS << "ChangeLog: block new " << BBInfo.Index << ' ' << BB.getName()
         << '\n';
    }
    OS << "ChangeLog: block final " << BBInfo.Index << '\n';
    for (auto &I : BB) {
      auto &IInfo = Instructions[&I];
      if (IInfo.PassNumber == 0) {
        IInfo.Index = InstructionNumber++;
        IInfo.BlockNumber = BBInfo.Index;
        OS << "ChangeLog: inst new " << IInfo.Index << ' ' << I << '\n';
      }
      if (IInfo.BlockNumber != BBInfo.Index) {
        OS << "ChangeLog: inst moved " << IInfo.Index << ' '
           << IInfo.BlockNumber << "->" << BBInfo.Index << ' ' << I << '\n';
        IInfo.BlockNumber = BBInfo.Index;
      }
      OS << "ChangeLog: inst final " << IInfo.Index << ' ' << I << '\n';
    }
  }
}

// Simple legacy pass manager interface which is added as a
// pass after other passes to report changes.
struct GraphChangeLogLegacyPass : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  raw_ostream &OS;
  const char *PassNameC;
  GraphChangeLogLegacyPass() : FunctionPass(ID), OS(dbgs()), PassNameC("") {
    initializeGraphChangeLogLegacyPassPass(*PassRegistry::getPassRegistry());
  }
  GraphChangeLogLegacyPass(raw_ostream &OS, const char *PassNameC)
      : FunctionPass(ID), OS(OS), PassNameC(PassNameC) {}

  bool runOnFunction(Function &F) override {
    if (!FinalPassSeen && F.getName() == FunctionToLog)
      afterPass(F, PassNameC, OS);
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

struct GraphChangeLogFinalLegacyPass : public FunctionPass {

  raw_ostream &OS;
  static char ID; // Pass identification, replacement for typeid
  GraphChangeLogFinalLegacyPass() : FunctionPass(ID), OS(dbgs()) {
    initializeGraphChangeLogFinalLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  GraphChangeLogFinalLegacyPass(raw_ostream &OS) : FunctionPass(ID), OS(OS) {}

  bool runOnFunction(Function &F) override {
    if (!FinalPassSeen && F.getName() == FunctionToLog)
      finalPass(F, OS);
    FinalPassSeen = true;
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};
}

char GraphChangeLogLegacyPass::ID = 0;
INITIALIZE_PASS(GraphChangeLogLegacyPass, "gcl", "Graph Change Logging", false,
                false)
char GraphChangeLogFinalLegacyPass::ID = 0;
INITIALIZE_PASS(GraphChangeLogFinalLegacyPass, "gclf",
                "Graph Change Logging Final", false, false)

FunctionPass *llvm::createGraphChangeLogLegacyPass(raw_ostream &OS,
                                                   const char *PassName) {
  if (FunctionToLog.empty())
    return nullptr;
  return new GraphChangeLogLegacyPass(OS, PassName);
}
FunctionPass *llvm::createGraphChangeLogFinalLegacyPass(raw_ostream &OS) {
  if (FunctionToLog.empty())
    return nullptr;
  return new GraphChangeLogFinalLegacyPass(OS);
}
