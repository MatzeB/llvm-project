// facebook T53546053
//===- CFGChangeLogManager.cpp - Shows CFG change with transformations ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements CFG change logger. It implements functions to be called
/// before and after the transformations, as well as data structures for CFG
/// change logging.
///
//===----------------------------------------------------------------------===//

#include "llvm/Passes/CFGChangeLogManager.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CFGChangeLogHandler.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ToolOutputFile.h"
#include <cassert>

namespace llvm {

cl::list<std::string>
    CFGChangeLogFuncs("cfg-change-log-funcs", cl::value_desc("function names"),
                      cl::desc("Only print CFG diff for functions whose name "
                               "match this"),
                      cl::CommaSeparated, cl::Hidden);

cl::opt<std::string>
    CFGChangeLogCfgDumpDir("cfg-change-log-cfg-dump-dir",
                           cl::value_desc("dir name"),
                           cl::desc("Dump CFGs after the transformations to "
                                    "the given directory"));

namespace {

//
// Data structures to represent CFG
//

// Outgoing edges are represented by the pointer value of destination node's
// corresponding block, (absolute) weight of the branch represented by the
// edge, and the branch probability.
using OutgoingEdge = std::tuple<const void *, int64_t, double>;

// Nodes are represendted by the pointer value of their corresponing
// (Machine)BasicBlock.
struct Node {
  Node(const void *Ptr, const std::string &Name, Optional<uint64_t> ProfCount,
       const std::vector<OutgoingEdge> &Outgoings)
      : Ptr(Ptr), Name(Name), ProfCount(ProfCount), Outgoings(Outgoings) {}

  // Check if two nodes are completely identical, including the profile
  // information for the node itself and its outgoing edges.
  bool isIdentical(const Node &N) const {
    if (Ptr != N.Ptr)
      return false;

    if (ProfCount && N.ProfCount) {
      if (*ProfCount != *N.ProfCount)
        return false;
    } else if (ProfCount || N.ProfCount)
      return false;

    if (Outgoings.size() != N.Outgoings.size())
      return false;

    unsigned i = 0;
    for (; i < Outgoings.size(); ++i)
      if (Outgoings[i] != N.Outgoings[i])
        return false;

    return true;
  }

  // Get string representation of the node.
  std::string getLabel() const {
    std::string Label;
    raw_string_ostream rso(Label);

    std::string ProfCountStr = ProfCount ? Twine(*ProfCount).str() : "N/A";
    rso << Name << "(" << Ptr << ", prof " << ProfCountStr << ")";
    return rso.str();
  }

  void print(raw_ostream &OS) const {
    std::string ProfCountStr = ProfCount ? Twine(*ProfCount).str() : "N/A";
    OS << getLabel();
    OS << ", outgoings ";
    for (auto &e : Outgoings)
      OS << std::get<0>(e) << ":" << std::get<1>(e) << "("
         << format("%.2f%%", std::get<2>(e)) << ") ";
  }

  const void *Ptr;
  std::string Name;
  Optional<uint64_t> ProfCount;
  std::vector<OutgoingEdge> Outgoings;
};

// Identify Node* with the pointing block's Ptr, not with the pointer value
// itself.
struct NodePtrHash {
  size_t operator()(const Node &N) const {
    return std::hash<const void *>()(N.Ptr);
  }
};

struct NodePtrEqual {
  bool operator()(const Node &N1, const Node &N2) const {
    return N1.Ptr == N2.Ptr;
  }
};

// A CFG is represented by a set of Nodes.
class CFG {
public:
  template <class T> static CFG *createCFG(const T *Func) {
    CFG *Cfg = new CFG();
    Cfg->template populateCFG<T>(Func);
    return Cfg;
  }

  const std::unordered_set<Node, NodePtrHash, NodePtrEqual> &getNodes() const {
    return Nodes;
  }

  bool hasNode(const Node &N) const { return Nodes.count(N); }

  // Print CFG in graphviz dot format.
  void print(raw_ostream &OS) const {
    OS << "digraph " << Name << " {\n";
    for (auto &N : Nodes) {
      OS << "  \"" << N.Ptr << "\" [label=\"" << N.getLabel() << "\"];\n";
      for (auto &E : N.Outgoings) {
        OS << "  \"" << N.Ptr << "\" -> \"" << std::get<0>(E) << "\" [label=\""
           << std::get<1>(E) << "(" << format("%.2f%%", std::get<2>(E))
           << ")\"];\n";
      }
    }
    OS << "}\n";
  }

private:
  CFG() = default;
  template <class T> void populateCFG(const T *);

  std::string Name;
  std::unordered_set<Node, NodePtrHash, NodePtrEqual> Nodes;
};

raw_ostream &operator<<(raw_ostream &OS, const Node &N) {
  N.print(OS);
  return OS;
}

template <> void CFG::populateCFG<Function>(const Function *F) {
  this->Name = std::string(F->getName());

  std::unique_ptr<DominatorTree> DT =
      std::make_unique<DominatorTree>(const_cast<Function &>(*F));
  std::unique_ptr<LoopInfo> LI = std::make_unique<LoopInfo>(*DT);
  std::unique_ptr<BranchProbabilityInfo> BPI =
      std::make_unique<BranchProbabilityInfo>(*F, *LI);
  std::unique_ptr<BlockFrequencyInfo> BFI =
      std::make_unique<BlockFrequencyInfo>(*F, *BPI, *LI);

  for (auto BI = F->begin(); BI != F->end(); ++BI) {
    auto *BB = &*BI;

    std::string Name;
    raw_string_ostream rso(Name);
    (*BI).printAsOperand(rso, false);

    std::vector<OutgoingEdge> Outgoings;
    const Instruction *TI = BB->getTerminator();
    MDNode *ProfMD = TI->getMetadata(LLVMContext::MD_prof);
    bool HasWeight = false;

    if (ProfMD && ProfMD->getNumOperands() == TI->getNumSuccessors() + 1 &&
        (isa<BranchInst>(TI) || isa<SwitchInst>(TI) || isa<IndirectBrInst>(TI)))
      HasWeight = true;

    unsigned int succIndex = 1;
    for (auto SI = succ_begin(BB); SI != succ_end(BB); ++SI, ++succIndex) {
      int64_t W = -1;
      if (HasWeight) {
        auto *Weight =
            mdconst::dyn_extract<ConstantInt>(ProfMD->getOperand(succIndex));
        if (Weight)
          W = Weight->getSExtValue();
      }
      BranchProbability Prob = BPI->getEdgeProbability(BB, SI);
      double Percent =
          rint(((double)Prob.getNumerator() / Prob.getDenominator()) * 100.0 *
               100.0) /
          100.0;
      Outgoings.emplace_back(static_cast<const void *>(*SI), W, Percent);
    }
    this->Nodes.insert(Node(static_cast<const void *>(BB), rso.str(),
                            BFI->getBlockProfileCount(BB), Outgoings));
  }
}

template <> void CFG::populateCFG<MachineFunction>(const MachineFunction *MF) {
  this->Name = std::string(MF->getName());

  std::unique_ptr<MachineDominatorTree> MDT =
      std::make_unique<MachineDominatorTree>();
  MDT->getBase().recalculate(const_cast<MachineFunction &>(*MF));

  std::unique_ptr<MachineLoopInfo> MLI = std::make_unique<MachineLoopInfo>();
  MLI->getBase().analyze(MDT->getBase());

  std::unique_ptr<MachineBranchProbabilityInfo> MBPI =
      std::make_unique<MachineBranchProbabilityInfo>();
  std::unique_ptr<MachineBlockFrequencyInfo> MBFI =
      std::make_unique<MachineBlockFrequencyInfo>();
  MBFI->calculate(*MF, *MBPI, *MLI);

  for (auto BI = MF->begin(); BI != MF->end(); ++BI) {
    auto *MBB = &*BI;

    std::string Name;
    raw_string_ostream rso(Name);
    rso << printMBBReference(*MBB);
    if (const auto *BB = MBB->getBasicBlock())
      if (BB->hasName())
        rso << "." << BB->getName();

    std::vector<OutgoingEdge> Outgoings;
    for (auto SI = MBB->succ_begin(); SI != MBB->succ_end(); ++SI) {
      BranchProbability Prob = MBPI->getEdgeProbability(MBB, SI);
      // We don't have raw branch weight metadata for MIR, so just use numerator
      // of BranchProbability.
      int64_t N = Prob.getNumerator();
      double Percent =
          rint(((double)N / Prob.getDenominator()) * 100.0 * 100.0) / 100.0;
      Outgoings.emplace_back(static_cast<const void *>(*SI), N, Percent);
    }

    this->Nodes.insert(Node(static_cast<const void *>(MBB), rso.str(),
                            MBFI->getBlockProfileCount(MBB), Outgoings));
  }
}

void printCFGChangeLog(raw_ostream &OS, const CFG *Before, const CFG *After) {
  assert(After != nullptr);
  auto NodesAfter = After->getNodes();

  bool changed = false;
  if (Before) {
    for (auto &N : Before->getNodes()) {
      // Check if NodesAfter has a node with same "val" with N.
      auto It = NodesAfter.find(N);
      if (It != NodesAfter.end()) {
        if (!N.isIdentical(*It)) {
          OS << "  CFGChangeLog: changed " << N << "\n"
             << "                     -> " << *It << "\n";
          changed = true;
        }
      } else {
        OS << "  CFGChangeLog: deleted " << N << "\n";
        changed = true;
      }
    }
  } else
    OS << "  CFGChangeLog: cfg created: " << After << "\n";

  for (auto &N : NodesAfter) {
    if (!Before || !Before->hasNode(N)) {
      OS << "  CFGChangeLog: inserted " << N << "\n";
      changed = true;
    }
  }

  if (!changed)
    OS << "  CFGChangeLog: no change\n";
}

static bool hasLoggingTarget() { return !CFGChangeLogFuncs.empty(); }

bool isLoggingTarget(StringRef FunctionName) {
  static std::unordered_set<std::string> Targets;
  static std::once_flag OnceFlag;
  auto Insert = []() {
    Targets.insert(CFGChangeLogFuncs.begin(), CFGChangeLogFuncs.end());
  };
  std::call_once(OnceFlag, Insert);
  return Targets.count("*") || Targets.count(std::string(FunctionName));
}

ManagedStatic<sys::SmartMutex<true>> CFGChangeLogMutex;
std::unordered_map<size_t, std::unique_ptr<CFG>> CFGs;
std::unordered_map<size_t, unsigned> CFGCounter;

// Get unique identifier in string for each function, considering its parent
// module.
template <class T> std::string getID(const T *F);
template <> std::string getID<Function>(const Function *F) {
  return F->getParent()->getName().str() + ":" + F->getName().str();
}
template <> std::string getID<MachineFunction>(const MachineFunction *MF) {
  auto &F = MF->getFunction();
  return F.getParent()->getName().str() + ":" + F.getName().str();
}

template <class FuncT>
static void runAfter(const FuncT *F, const std::string &Banner) {
  auto ID = getID<FuncT>(F);
  auto IDNum = std::hash<std::string>{}(ID);
  unsigned Counter = 0;
  {
    sys::SmartScopedLock<true> Lock(*CFGChangeLogMutex);
    Counter = ++CFGCounter[IDNum];
  }
  dbgs() << Banner << " {" << ID << ":" << IDNum << ":" << Counter << "} "
         << "\n";

  CFG *Before = nullptr;
  {
    sys::SmartScopedLock<true> Lock(*CFGChangeLogMutex);
    auto It = CFGs.find(IDNum);
    if (It != CFGs.end())
      Before = It->second.get();
  }

  std::unique_ptr<CFG> CFGAfter(CFG::createCFG<FuncT>(F));
  printCFGChangeLog(dbgs(), Before, CFGAfter.get());

  if (!CFGChangeLogCfgDumpDir.empty()) {
    std::string Path = CFGChangeLogCfgDumpDir +
                       sys::path::get_separator().data() + Twine(IDNum).str() +
                       "." + Twine(Counter).str() + ".dot";
    std::error_code EC;
    auto DumpFile =
        std::make_unique<ToolOutputFile>(Path, EC, llvm::sys::fs::OF_None);
    if (EC) {
      std::string Msg = "Error: " + Path + ", " + EC.message();
      llvm_unreachable(Msg.c_str());
    }

    CFGAfter->print(DumpFile->os());
    DumpFile->keep();
  }

  {
    sys::SmartScopedLock<true> Lock(*CFGChangeLogMutex);
    CFGs[IDNum] = std::move(CFGAfter);
  }
}

std::function<void(const Function *, const std::string &)> runAfterIR =
    runAfter<Function>;
std::function<void(const MachineFunction *, const std::string &)> runAfterMIR =
    runAfter<MachineFunction>;

} // namespace

static std::once_flag RegisterCallbacksOnceFlag;

void CFGChangeLogManager::registerCallbacks(PassInstrumentationCallbacks &PIC) {
  // Although registerCallbacks is for the new pass manager, we piggyback on
  // its interface to register callbacks for the legacy pass manager.
  //
  // Lines above CFGChangeLogFuncs emptiness checking are fore the legacy pass
  // manager. For legacy pass manager, we need to register callbacks even when
  // CFGChangeLogFuncs is empty, because checking CFGChangeLogFuncs itself
  // requires callback.
  //
  // For new pass manager, we don't even register the callback function if
  // CFGChangeLogFuncs is empty to avoid unnecessary overhead.

  auto Register = []() {
    auto &LH = getCFGChangeLogHandler<Function>();
    LH.registerHasLoggingTarget(hasLoggingTarget);
    LH.registerIsLoggingTarget(isLoggingTarget);
    LH.registerRunAfter(runAfter<Function>);

    auto &MH = getCFGChangeLogHandler<MachineFunction>();
    MH.registerHasLoggingTarget(hasLoggingTarget);
    MH.registerIsLoggingTarget(isLoggingTarget);
    MH.registerRunAfter(runAfter<MachineFunction>);
  };
  std::call_once(RegisterCallbacksOnceFlag, Register);

  if (CFGChangeLogFuncs.empty())
    return;

  PIC.registerAfterPassCallback(
      [this](StringRef P, Any IR, const PreservedAnalyses &) {
        this->runAfterPass(P, IR);
      });
}

// Callback to run after each transformation for the new pass manager.
void CFGChangeLogManager::runAfterPass(StringRef PassID, Any IR) {
  if (PassID.startswith("PassManager<") || PassID.contains("PassAdaptor<"))
    return;

  auto GetBannerPrefix = [&PassID](const Function &F) {
    std::string PassIDStr = "*** CFGChangeLog After " + std::string(PassID);
    return PassIDStr + " (function: " + F.getName().str() + ")";
  };

  if (any_isa<const Module *>(IR)) {
    const Module *M = any_cast<const Module *>(IR);

    for (const Function &F : *M)
      if (!F.isDeclaration() && isLoggingTarget(F.getName()))
        runAfterIR(&F, GetBannerPrefix(F));
  } else if (any_isa<const Function *>(IR)) {
    const Function *F = any_cast<const Function *>(IR);
    if (!F->isDeclaration() && isLoggingTarget(F->getName()))
      runAfterIR(F, GetBannerPrefix(*F));
  } else if (any_isa<const LazyCallGraph::SCC *>(IR)) {
    const LazyCallGraph::SCC *C = any_cast<const LazyCallGraph::SCC *>(IR);
    for (const LazyCallGraph::Node &N : *C) {
      const Function *F = &(N.getFunction());
      if (!F->isDeclaration() && isLoggingTarget(F->getName()))
        runAfterIR(F, GetBannerPrefix(*F));
    }
  } else if (any_isa<const Loop *>(IR)) {
    const Loop *L = any_cast<const Loop *>(IR);
    const Function *F = L->getHeader()->getParent();
    if (!F->isDeclaration() && isLoggingTarget(F->getName())) {
      auto Banner = GetBannerPrefix(*F) +
                    " (loop: " + L->getHeader()->getName().str() + ")";
      runAfterIR(F, Banner);
    }
  } else
    llvm_unreachable("Unknown IR unit");
}

} // namespace llvm
