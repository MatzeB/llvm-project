// facebook T68973288
//===- SampleProfileInference.cpp - Adjust sample profiles in the IR ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a profile inference algorithm. Given an incomplete and
// possibly imprecise block counts, the algorithm reconstructs realistic block
// and edge counts that satisfy flow conservation rules, while minimally modify
// input block counts.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/SampleProfileInference.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/Debug.h"
#include <queue>

using namespace llvm;
#define DEBUG_TYPE "sample-profile-inference"

namespace {

/// A wrapper of a binary basic block.
struct FlowBlock {
  uint64_t Weight{0};
  bool Dangling{false};
  uint64_t Flow{0};
  uint64_t InDegree{0};
  uint64_t OutDegree{0};
  bool HasSelfEdge{false};
};

/// A wrapper of a jump between two basic blocks.
struct FlowJump {
  uint64_t Source;
  uint64_t Target;
  uint64_t Flow{0};
};

/// A wrapper of binary function with basic blocks and jumps.
struct FlowFunction {
  std::vector<FlowBlock> Blocks;
  std::vector<FlowJump> Jumps;
};

/// Minimum-cost maximum flow algorithm.
///
/// The algorithm finds the maximum flow of minimum cost on a given (directed)
/// network using the Edmonds-Karp approach, also known as successive shortest
/// path and capacity scaling. The estimated time complexity is
/// O(m*log(m)*flow), where m is the number of edges and flow is the max flow.
///
/// The input is a set of edges with specified costs and capacities, and a pair
/// of vertices (source and sink). The output is the flow along each edge of the
/// minimum total cost respecting the given edge capacities.
class MinCostFlow {
public:
  // Initialize algorithm's data structures for a netwrok of a given size.
  void initialize(uint64_t NodeCount, uint64_t Source_, uint64_t Target_) {
    Source = Source_;
    Target = Target_;

    Nodes = std::vector<Node>(NodeCount);
    Edges = std::vector<std::vector<Edge>>(NodeCount, std::vector<Edge>());
  }

  // Run the algorithm.
  int64_t run() {
    // Find an augmenting path and update the flow along the path
    while (findAugmentingPath()) {
      updateAugmentingPath();
    }

    // Compute the total flow and its cost
    int64_t TotalCost = 0;
    int64_t TotalFlow = 0;
    for (uint64_t Src = 0; Src < Nodes.size(); Src++) {
      for (auto &Edge : Edges[Src]) {
        if (Edge.Flow > 0) {
          TotalCost += Edge.Cost * Edge.Flow;
          if (Src == Source)
            TotalFlow += Edge.Flow;
        }
      }
    }
    return TotalCost;
  }

  /// Adding an edge to the network with a specified capacity and a cost.
  /// Multiple edges between a pair of vertices are allowed but self-edges
  /// are not supported.
  void addEdge(uint64_t Src, uint64_t Dst, int64_t Capacity, int64_t Cost) {
    assert(Capacity > 0 && "adding an edge of zero capacity");
    assert(Src != Dst && "loop edge are not supported");

    Edge SrcEdge;
    SrcEdge.Dst = Dst;
    SrcEdge.Cost = Cost;
    SrcEdge.Capacity = Capacity;
    SrcEdge.Flow = 0;
    SrcEdge.RevEdgeIndex = Edges[Dst].size();

    Edge DstEdge;
    DstEdge.Dst = Src;
    DstEdge.Cost = -Cost;
    DstEdge.Capacity = 0;
    DstEdge.Flow = 0;
    DstEdge.RevEdgeIndex = Edges[Src].size();

    Edges[Src].push_back(SrcEdge);
    Edges[Dst].push_back(DstEdge);
  }

  /// Adding an edge to the network of infinite capacity and a given cost.
  void addEdge(uint64_t Src, uint64_t Dst, int64_t Cost) {
    addEdge(Src, Dst, INF, Cost);
  }

  /// Collect adjacencies of the given node in the network.
  const std::vector<uint64_t> adjacentNodes(uint64_t Src) const {
    std::vector<uint64_t> Adjacent;
    for (auto &Edge : Edges[Src]) {
      if (Edge.Flow > 0) {
        Adjacent.push_back(Edge.Dst);
      }
    }
    return Adjacent;
  }

  /// Get the total flow between a pair of vertices.
  int64_t getFlow(uint64_t Src, uint64_t Dst) const {
    int64_t Flow = 0;
    for (auto &Edge : Edges[Src]) {
      if (Edge.Dst == Dst) {
        Flow += Edge.Flow;
      }
    }
    return Flow;
  }

  /// A cost of increasing a block's count by one.
  static constexpr int64_t AuxCostInc = 1;
  /// A cost of decreasing a block's count by one.
  static constexpr int64_t AuxCostDec = 2;
  /// A cost of decreasing the entry block's count by one.
  static constexpr int64_t AuxCostDecEntry = ((int64_t)1) << 20;

private:
  /// Check for existence of an augmenting path with a positive capacity.
  bool findAugmentingPath() {
    for (auto &Node : Nodes) {
      Node.Distance = INF;
      Node.ParentNode = uint64_t(-1);
      Node.ParentEdgeIndex = uint64_t(-1);
      Node.Taken = false;
    }

    std::queue<uint64_t> Queue;
    Queue.push(Source);
    Nodes[Source].Distance = 0;
    Nodes[Source].Taken = true;
    while (!Queue.empty()) {
      uint64_t Src = Queue.front();
      Queue.pop();
      Nodes[Src].Taken = false;
      // Process adjacent edges
      for (uint64_t I = 0; I < Edges[Src].size(); I++) {
        auto &Edge = Edges[Src][I];
        if (Edge.Flow < Edge.Capacity) {
          uint64_t Dst = Edge.Dst;
          if (Nodes[Dst].Distance > Nodes[Src].Distance + Edge.Cost) {
            // Update the distance and the parent node/edge
            Nodes[Dst].Distance = Nodes[Src].Distance + Edge.Cost;
            Nodes[Dst].ParentNode = Src;
            Nodes[Dst].ParentEdgeIndex = I;
            // Add the node to the queue, if it is not there yet
            if (!Nodes[Dst].Taken) {
              Queue.push(Dst);
              Nodes[Dst].Taken = true;
            }
          }
        }
      }
    }

    return Nodes[Target].Distance != INF;
  }

  /// Update the current flow along the augmenting path.
  void updateAugmentingPath() {
    // Find path capacity
    int64_t PathCapacity = INF;
    uint64_t Now = Target;
    while (Now != Source) {
      uint64_t Pred = Nodes[Now].ParentNode;
      auto &Edge = Edges[Pred][Nodes[Now].ParentEdgeIndex];
      PathCapacity = std::min(PathCapacity, Edge.Capacity - Edge.Flow);
      Now = Pred;
    }

    assert(PathCapacity > 0 && "found incorrect augmenting path");

    // Update the flow along the path
    Now = Target;
    while (Now != Source) {
      uint64_t Pred = Nodes[Now].ParentNode;
      auto &Edge = Edges[Pred][Nodes[Now].ParentEdgeIndex];
      auto &RevEdge = Edges[Now][Edge.RevEdgeIndex];

      Edge.Flow += PathCapacity;
      RevEdge.Flow -= PathCapacity;

      Now = Pred;
    }
  }

  /// A value indicating an infinite flow/capacity of an edge.
  /// Not using numeric_limits<int64_t>::max(), as the values can be summed up
  /// during the execution.
  static constexpr int64_t INF = ((int64_t)1) << 40;

  /// An node in a flow network.
  struct Node {
    /// The cost of the cheapest path from the source to the current node.
    int64_t Distance;
    /// The node preceding the current one in the path.
    uint64_t ParentNode;
    /// The index of the edge between ParentNode and the current node.
    uint64_t ParentEdgeIndex;
    /// An indicator of whether the current node is in a queue.
    bool Taken;
  };
  /// An edge in a flow network.
  struct Edge {
    /// The cost of the edge.
    int64_t Cost;
    /// The capacity of the edge.
    int64_t Capacity;
    /// The current flow on the edge.
    int64_t Flow;
    /// The destination node of the edge.
    uint64_t Dst;
    /// The index of the reverse edge between Dst and the current node.
    uint64_t RevEdgeIndex;
  };

  /// The set of network nodes.
  std::vector<Node> Nodes;
  /// The set of network edges.
  std::vector<std::vector<Edge>> Edges;
  /// Source node of the flow.
  uint64_t Source;
  /// Target (sink) node of the flow.
  uint64_t Target;
};

/// Initializing flow network for a given function.
///
/// Every block is split into three nodes that are responsible for (i) an
/// incoming flow, (ii) an outgoing flow, and (iii) penalizing an increase or
/// reduction of the block weight.
void initializeNetwork(MinCostFlow &Network, FlowFunction &Func) {
  uint64_t NumBlocks = Func.Blocks.size();
  assert(NumBlocks > 1 && "Too few blocks in a function");

  // Pre-process data: make sure the entry weight is at least 1
  for (uint64_t B = 0; B < NumBlocks; B++) {
    if (Func.Blocks[B].InDegree == 0 && Func.Blocks[B].Weight == 0)
      Func.Blocks[B].Weight = 1;
  }

  // Introducing dummy source/sink pairs to allow flow circulation.
  // The nodes corresponding to blocks of Func have indicies in the range
  // [0..3 * NumBlocks); the dummy nodes are indexed by the next four values.
  uint64_t S = 3 * NumBlocks;
  uint64_t T = S + 1;
  uint64_t S1 = S + 2;
  uint64_t T1 = S + 3;

  Network.initialize(3 * NumBlocks + 4, S1, T1);

  // Create three nodes for every block of the function
  for (uint64_t B = 0; B < NumBlocks; B++) {
    auto &Block = Func.Blocks[B];
    bool IsEntry = Block.InDegree == 0;
    bool IsExit = Block.OutDegree == 0;
    assert((!Block.Dangling || Block.Weight == 0 || IsEntry) &&
           "non-zero weight of a dangling block except for a dangling entry");

    // Split every block into two nodes
    uint64_t Bin = 3 * B;
    uint64_t Bout = 3 * B + 1;
    if (Block.Weight > 0) {
      Network.addEdge(S1, Bout, Block.Weight, 0);
      Network.addEdge(Bin, T1, Block.Weight, 0);
    }

    // Edges from S and to T
    assert((!IsEntry || !IsExit) && "a block cannot be an entry and an exit");
    if (IsEntry) {
      Network.addEdge(S, Bin, 0);
    } else if (IsExit) {
      Network.addEdge(Bout, T, 0);
    }

    // An auxiliary node to allow increase/reduction of block counts
    uint64_t Baux = 3 * B + 2;
    int64_t AuxCostInc = MinCostFlow::AuxCostInc;
    int64_t AuxCostDec = MinCostFlow::AuxCostDec;
    // Do not penalize changing weights of dangling blocks
    if (Block.Dangling) {
      AuxCostInc = 0;
      AuxCostDec = 0;
    }
    // Decreasing the weight of entry blocks is expensive
    if (IsEntry) {
      AuxCostDec = MinCostFlow::AuxCostDecEntry;
    }
    // For blocks with self-edges, do not penalize a reduction of the weight,
    // as all of the weight can be attributed to the self-edge
    if (Block.HasSelfEdge) {
      AuxCostDec = 0;
    }

    Network.addEdge(Bin, Baux, AuxCostInc);
    Network.addEdge(Baux, Bout, AuxCostInc);
    Network.addEdge(Bout, Baux, AuxCostDec);
    Network.addEdge(Baux, Bin, AuxCostDec);
  }

  // Creating edges for every jump
  for (auto &Jump : Func.Jumps) {
    uint64_t Src = Jump.Source;
    uint64_t Dst = Jump.Target;
    if (Src != Dst) {
      uint64_t SrcOut = 3 * Src + 1;
      uint64_t DstIn = 3 * Dst;
      Network.addEdge(SrcOut, DstIn, 0);
    }
  }

  // Make sure we have a valid flow circulation
  Network.addEdge(T, S, 0);
}

/// Extract resulting block and edge counts from the flow network.
void extractWeights(MinCostFlow &Network, FlowFunction &Func) {
  uint64_t NumBlocks = Func.Blocks.size();

  // Extract resulting block counts
  for (uint64_t Src = 0; Src < NumBlocks; Src++) {
    auto &Block = Func.Blocks[Src];
    uint64_t SrcOut = 3 * Src + 1;
    int64_t Flow = 0;
    for (uint64_t Dst : Network.adjacentNodes(SrcOut)) {
      bool IsAuxNode = (Dst < 3 * NumBlocks && Dst % 3 == 2);
      if (!IsAuxNode || Block.HasSelfEdge) {
        Flow += Network.getFlow(SrcOut, Dst);
      }
    }
    Block.Flow = Flow;
    assert(Flow >= 0 && "negative block flow");
  }

  // Extract resulting jump counts
  for (auto &Jump : Func.Jumps) {
    uint64_t Src = Jump.Source;
    uint64_t Dst = Jump.Target;
    int64_t Flow = 0;
    if (Src != Dst) {
      uint64_t SrcOut = 3 * Src + 1;
      uint64_t DstIn = 3 * Dst;
      Flow = Network.getFlow(SrcOut, DstIn);
    } else {
      uint64_t SrcOut = 3 * Src + 1;
      uint64_t SrcAux = 3 * Src + 2;
      int64_t AuxFlow = Network.getFlow(SrcOut, SrcAux);
      if (AuxFlow > 0)
        Flow = AuxFlow;
    }
    Jump.Flow = Flow;
    assert(Flow >= 0 && "negative jump flow");
  }
}

#ifndef NDEBUG
/// Verify that the computed flow values satisfy flow conservation rules
void verifyWeights(const FlowFunction &Func) {
  uint64_t NumBlocks = Func.Blocks.size();
  auto InFlow = std::vector<uint64_t>(NumBlocks, 0);
  auto OutFlow = std::vector<uint64_t>(NumBlocks, 0);
  for (auto &Jump : Func.Jumps) {
    InFlow[Jump.Target] += Jump.Flow;
    OutFlow[Jump.Source] += Jump.Flow;
  }

  uint64_t TotalInFlow = 0;
  uint64_t TotalOutFlow = 0;
  for (uint64_t I = 0; I < NumBlocks; I++) {
    auto &Block = Func.Blocks[I];
    if (Block.InDegree == 0) {
      TotalInFlow += Block.Flow;
      assert(Block.Flow == OutFlow[I] && "incorrectly computed flow");
    } else if (Block.OutDegree == 0) {
      TotalOutFlow += Block.Flow;
      assert(Block.Flow == InFlow[I] && "incorrectly computed flow");
    } else {
      assert(Block.Flow == OutFlow[I] && "incorrectly computed flow");
      assert(Block.Flow == InFlow[I] && "incorrectly computed flow");
    }
  }
  assert(TotalInFlow == TotalOutFlow && "incorrectly computed flow");
}
#endif

} // end of anonymous namespace

/// Apply the profile inference algorithm for a given function
void SampleProfileInference::apply(BlockWeightMap &BlockWeights,
                                   EdgeWeightMap &EdgeWeights) {
  // Find all reachable blocks which the inference algorithm will be applied on.
  df_iterator_default_set<const BasicBlock *> Reachable;
  for (auto *BB : depth_first_ext(&F, Reachable))
    (void)BB /* Mark all reachable blocks */;

  BlockWeights.clear();
  EdgeWeights.clear();
  bool HasSamples = false;
  for (const auto *BB : Reachable) {
    auto It = SampleBlockWeights.find(BB);
    if (It != SampleBlockWeights.end() && It->second > 0) {
      HasSamples = true;
      BlockWeights[BB] = It->second;
    }
  }
  // Quit early for functions with a single block or ones w/o samples
  if (Reachable.size() <= 1 || !HasSamples) {
    return;
  }

  // Create necessary objects
  FlowFunction Func;
  DenseMap<const BasicBlock *, uint64_t> BlockIndex;
  std::vector<const BasicBlock *> AllBlocks;
  BlockIndex.reserve(Reachable.size());
  AllBlocks.reserve(Reachable.size());
  Func.Blocks.reserve(Reachable.size());
  // Process blocks
  for (const auto *BB : Reachable) {
    BlockIndex[BB] = AllBlocks.size();
    AllBlocks.push_back(BB);
    FlowBlock Block;
    if (SampleBlockWeights.find(BB) != SampleBlockWeights.end()) {
      Block.Dangling = false;
      Block.Weight = SampleBlockWeights[BB];
    } else {
      Block.Dangling = true;
      Block.Weight = 0;
    }
    Func.Blocks.push_back(Block);
  }
  // Process edges
  for (const auto *BB : Reachable) {
    for (auto *Succ : Successors[BB]) {
      FlowJump Jump;
      Jump.Source = BlockIndex[BB];
      Jump.Target = BlockIndex[Succ];
      Func.Jumps.push_back(Jump);
      Func.Blocks[BlockIndex[BB]].OutDegree++;
      Func.Blocks[BlockIndex[Succ]].InDegree++;
      if (BB == Succ) {
        Func.Blocks[BlockIndex[BB]].HasSelfEdge = true;
      }
    }
  }

  // Create inference network model
  auto InferenceNetwork = MinCostFlow();
  initializeNetwork(InferenceNetwork, Func);

  // Run the inference algorithm
  InferenceNetwork.run();

  // Verify the result and extract flow values
  extractWeights(InferenceNetwork, Func);
#ifndef NDEBUG
  verifyWeights(Func);
#endif

  // Extract the resulting weights
  for (const auto *BB : Reachable) {
    BlockWeights[BB] = Func.Blocks[BlockIndex[BB]].Flow;
  }
  for (auto &Jump : Func.Jumps) {
    Edge E = std::make_pair(AllBlocks[Jump.Source], AllBlocks[Jump.Target]);
    EdgeWeights[E] = Jump.Flow;
  }

#ifndef NDEBUG
  // Unreachable blocks and edges should not have a weight.
  for (auto &I : BlockWeights)
    assert(Reachable.contains(I.first));
  for (auto &I : EdgeWeights)
    assert(Reachable.contains(I.first.first) &&
           Reachable.contains(I.first.second));
#endif
}
