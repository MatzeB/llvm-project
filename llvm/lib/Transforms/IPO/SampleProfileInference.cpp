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
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include <queue>
#include <set>

using namespace llvm;
#define DEBUG_TYPE "sample-profile-inference"

namespace {

/// A value indicating an infinite flow/capacity/weight of a block/edge.
/// Not using numeric_limits<int64_t>::max(), as the values can be summed up
/// during the execution.
static constexpr int64_t INF = ((int64_t)1) << 40;

struct FlowJump;

/// A wrapper of a binary basic block.
struct FlowBlock {
  uint64_t Index;
  uint64_t Weight{0};
  bool Dangling{false};
  uint64_t Flow{0};
  bool HasSelfEdge{false};
  std::vector<FlowJump *> SuccJumps;
  std::vector<FlowJump *> PredJumps;

  /// In-degree of the block.
  size_t InDegree() const { return PredJumps.size(); }

  /// Out-degree of the block.
  size_t OutDegree() const { return SuccJumps.size(); }

  /// Check if it is the entry block in the function.
  bool isEntry() const { return PredJumps.empty(); }

  /// Check if it is an exit block in the function.
  bool isExit() const { return SuccJumps.empty(); }
};

/// A wrapper of a jump between two basic blocks.
struct FlowJump {
  uint64_t Source;
  uint64_t Target;
  uint64_t Flow{0};
  bool IsUnlikely{false};
};

/// A wrapper of binary function with basic blocks and jumps.
struct FlowFunction {
  std::vector<FlowBlock> Blocks;
  std::vector<FlowJump> Jumps;
  /// The index of the entry block.
  uint64_t Entry;
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
  /// A cost of increasing the entry block's count by one.
  static constexpr int64_t AuxCostIncEntry = ((int64_t)1) << 20;
  /// A cost of decreasing the entry block's count by one.
  static constexpr int64_t AuxCostDecEntry = ((int64_t)1) << 20;
  /// A cost of taking an unlikely jump.
  static constexpr int64_t AuxCostUnlikely = ((int64_t)1) << 20;

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

/// Post-processing adjustment of the control flow.
class FlowAdjuster {
public:
  FlowAdjuster(FlowFunction &Func, const bool RebalanceDangling)
      : Func(Func), RebalanceDangling(RebalanceDangling) {
    assert(Func.Blocks[Func.Entry].isEntry() &&
           "incorrect index of the entry block");
  }

  // Run the algorithms
  void run() {
    /// We adjust the control flow in a function so as to remove all
    /// "isolated" components with positive flow that are unreachable
    /// from the entry block. For every such component, we find the shortest
    /// path from the entry to an exit passing through the component, and
    /// increase the flow by one unit along the path.
    joinIsolatedComponents();

    /// A dangling subgraph comprises two nodes u and v such that:
    ///   - u dominates v and u is not dangling;
    ///   - v post-dominates u; and
    ///   - All inner-nodes of all (u,v)-paths are dangling
    /// This processor rebalances the flow that goes through the subgraphs.
    if (RebalanceDangling)
      rebalanceDanglingSubgraphs();
  }

  /// The probability for the first successor of a dangling subgraph
  static constexpr double DanglingFirstSuccProbability = 0.5;

private:
  void joinIsolatedComponents() {
    // Find blocks that are reachable from the source
    auto Visited = std::vector<bool>(NumBlocks(), false);
    findReachable(Func.Entry, Visited);

    // Iterate over all non-reachable blocks and adjust their weights
    for (uint64_t I = 0; I < NumBlocks(); I++) {
      auto &Block = Func.Blocks[I];
      if (Block.Flow > 0 && !Visited[I]) {
        // Find a path from the entry to an exit passing through the block I
        auto Path = findShortestPath(I);
        // Increase the flow along the path
        assert(Path.size() > 0 && Path[0]->Source == Func.Entry &&
               "incorrectly computed path adjusting control flow");
        Func.Blocks[Func.Entry].Flow += 1;
        for (auto &Jump : Path) {
          Jump->Flow += 1;
          Func.Blocks[Jump->Target].Flow += 1;
          // Update reachability
          findReachable(Jump->Target, Visited);
        }
      }
    }
  }

  /// Run bfs from a given block along the jumps with a positive flow and mark
  /// all reachable blocks.
  void findReachable(uint64_t Src, std::vector<bool> &Visited) {
    if (Visited[Src])
      return;
    std::queue<uint64_t> Queue;
    Queue.push(Src);
    Visited[Src] = true;
    while (!Queue.empty()) {
      Src = Queue.front();
      Queue.pop();
      for (auto Jump : Func.Blocks[Src].SuccJumps) {
        uint64_t Dst = Jump->Target;
        if (Jump->Flow > 0 && !Visited[Dst]) {
          Queue.push(Dst);
          Visited[Dst] = true;
        }
      }
    }
  }

  /// Find the shortest path from the entry block to an exit block passing
  /// through a given block.
  std::vector<FlowJump *> findShortestPath(uint64_t BlockIdx) {
    // A path from the entry block to BlockIdx
    auto ForwardPath = findShortestPath(Func.Entry, BlockIdx);
    // A path from BlockIdx to an exit block
    auto BackwardPath = findShortestPath(BlockIdx, AnyExitBlock);

    // Concatenate the two paths
    std::vector<FlowJump *> Result;
    Result.insert(Result.end(), ForwardPath.begin(), ForwardPath.end());
    Result.insert(Result.end(), BackwardPath.begin(), BackwardPath.end());
    return Result;
  }

  /// Apply the dijkstra algorithm to find the shortest path from a given
  /// Source to a given Target block.
  /// If Target == -1, then the path ends at an exit block.
  std::vector<FlowJump *> findShortestPath(uint64_t Source, uint64_t Target) {
    // Quit early, if possible
    if (Source == Target)
      return std::vector<FlowJump *>();
    if (Func.Blocks[Source].isExit() && Target == AnyExitBlock)
      return std::vector<FlowJump *>();

    // Initialize data structures
    auto Distance = std::vector<int64_t>(NumBlocks(), INF);
    auto Parent = std::vector<FlowJump *>(NumBlocks(), nullptr);
    Distance[Source] = 0;
    std::set<std::pair<uint64_t, uint64_t>> Queue;
    Queue.insert(std::make_pair(Distance[Source], Source));

    // Run the dijkstra algorithm
    while (!Queue.empty()) {
      uint64_t Src = Queue.begin()->second;
      Queue.erase(Queue.begin());
      // If we found a solution, quit early
      if (Src == Target ||
          (Func.Blocks[Src].isExit() && Target == AnyExitBlock))
        break;

      for (auto Jump : Func.Blocks[Src].SuccJumps) {
        uint64_t Dst = Jump->Target;
        int64_t JumpDist = jumpDistance(Jump);
        if (Distance[Dst] > Distance[Src] + JumpDist) {
          Queue.erase(std::make_pair(Distance[Dst], Dst));

          Distance[Dst] = Distance[Src] + JumpDist;
          Parent[Dst] = Jump;

          Queue.insert(std::make_pair(Distance[Dst], Dst));
        }
      }
    }
    // If Target is not provided, find the closest exit block
    if (Target == AnyExitBlock) {
      for (uint64_t I = 0; I < NumBlocks(); I++) {
        if (Func.Blocks[I].isExit() && Parent[I] != nullptr) {
          if (Target == AnyExitBlock || Distance[Target] > Distance[I]) {
            Target = I;
          }
        }
      }
    }
    assert(Parent[Target] != nullptr && "a path does not exist");

    // Extract the constructed path
    std::vector<FlowJump *> Result;
    uint64_t Now = Target;
    while (Now != Source) {
      assert(Now == Parent[Now]->Target && "incorrect parent jump");
      Result.push_back(Parent[Now]);
      Now = Parent[Now]->Source;
    }
    // Reverse the path, since it is extracted from Target to Source
    std::reverse(Result.begin(), Result.end());
    return Result;
  }

  /// A distance of a path for a given jump.
  /// In order to incite the path to use blocks/jumps with large positive flow,
  /// and avoid changing branch probabiliy of outging edges drastically,
  /// set the distance as follows:
  ///   if Jump.Flow > 0, then distance = max(100 - Jump->Flow, 0)
  ///   if Block.Weight > 0, then distance = 1
  ///   otherwise distance >> 1
  int64_t jumpDistance(FlowJump *Jump) const {
    int64_t BaseDistance = 100;
    if (Jump->IsUnlikely)
      return MinCostFlow::AuxCostUnlikely;
    if (Jump->Flow > 0)
      return std::max(BaseDistance - (int64_t)Jump->Flow, (int64_t)0);
    if (Func.Blocks[Jump->Target].Weight > 0)
      return BaseDistance;
    return BaseDistance * (NumBlocks() + 1);
  };

  uint64_t NumBlocks() const { return Func.Blocks.size(); }

  /// Rebalance dangling subgraphs so as each branch splits with probabilities
  /// DanglingFirstSuccProbability and 1 - DanglingFirstSuccProbability
  void rebalanceDanglingSubgraphs() {
    assert(DanglingFirstSuccProbability >= 0.0 &&
           DanglingFirstSuccProbability <= 1.0 &&
           "the share of the dangling successor should be between 0 and 1");
    // Try to find dangling subgraphs from each non-dangling block
    for (uint64_t I = 0; I < Func.Blocks.size(); I++) {
      auto SrcBlock = &Func.Blocks[I];
      // Do not attempt to find dangling successors from a dangling or a
      // zero-flow block
      if (SrcBlock->Dangling || SrcBlock->Flow == 0)
        continue;

      std::vector<FlowBlock *> DanglingSuccs;
      FlowBlock *DstBlock = nullptr;
      // Find a dangling subgraphs starting at block SrcBlock
      if (!findDanglingSubgraph(SrcBlock, DstBlock, DanglingSuccs))
        continue;
      // At the moment, we do not rebalance subgraphs containing cycles among
      // dangling blocks
      if (!isAcyclicSubgraph(SrcBlock, DstBlock, DanglingSuccs))
        continue;

      // Rebalance the flow
      rebalanceDanglingSubgraph(SrcBlock, DstBlock, DanglingSuccs);
    }
  }

  /// Find a dangling subgraph starting at block SrcBlock.
  /// If the search is successful, the method sets DstBlock and DanglingSuccs.
  bool findDanglingSubgraph(FlowBlock *SrcBlock, FlowBlock *&DstBlock,
                            std::vector<FlowBlock *> &DanglingSuccs) {
    // Run BFS from SrcBlock and make sure all paths are going through dangling
    // blocks and end at a non-dangling DstBlock
    auto Visited = std::vector<bool>(NumBlocks(), false);
    std::queue<uint64_t> Queue;
    DstBlock = nullptr;

    Queue.push(SrcBlock->Index);
    Visited[SrcBlock->Index] = true;
    while (!Queue.empty()) {
      auto &Block = Func.Blocks[Queue.front()];
      Queue.pop();
      // Process blocks reachable from Block
      for (auto Jump : Block.SuccJumps) {
        uint64_t Dst = Jump->Target;
        if (Visited[Dst])
          continue;
        Visited[Dst] = true;
        if (!Func.Blocks[Dst].Dangling) {
          // If we see non-unique non-dangling block reachable from SrcBlock,
          // stop processing and skip rebalancing
          FlowBlock *CandidateDstBlock = &Func.Blocks[Dst];
          if (DstBlock != nullptr && DstBlock != CandidateDstBlock)
            return false;
          DstBlock = CandidateDstBlock;
        } else {
          Queue.push(Dst);
          DanglingSuccs.push_back(&Func.Blocks[Dst]);
        }
      }
    }

    // If the list of dangling blocks is empty, we don't need rebalancing
    if (DanglingSuccs.empty())
      return false;
    // If there is no unique non-dangling destination block, skip rebalancing
    if (DstBlock == nullptr)
      return false;
    // If any of the dangling blocks is an exit block, skip rebalancing
    for (auto Block : DanglingSuccs) {
      if (Block->isExit())
        return false;
    }

    return true;
  }

  /// Verify if the given dangling subgraph is acyclic, and if yes, reorder
  /// DanglingSuccs in the topological order (so that all jumps are "forward").
  bool isAcyclicSubgraph(FlowBlock *SrcBlock, FlowBlock *DstBlock,
                         std::vector<FlowBlock *> &DanglingSuccs) {
    // Extract local in-degrees in the considered subgraph
    auto LocalInDegree = std::vector<uint64_t>(NumBlocks(), 0);
    for (auto Jump : SrcBlock->SuccJumps) {
      LocalInDegree[Jump->Target]++;
    }
    for (size_t I = 0; I < DanglingSuccs.size(); I++) {
      for (auto Jump : DanglingSuccs[I]->SuccJumps) {
        LocalInDegree[Jump->Target]++;
      }
    }
    // A loop containing SrcBlock
    if (LocalInDegree[SrcBlock->Index] > 0)
      return false;

    std::vector<FlowBlock *> AcyclicOrder;
    std::queue<uint64_t> Queue;
    Queue.push(SrcBlock->Index);
    while (!Queue.empty()) {
      auto &Block = Func.Blocks[Queue.front()];
      Queue.pop();
      // Stop propagation once we reach DstBlock
      if (Block.Index == DstBlock->Index)
        break;

      AcyclicOrder.push_back(&Block);
      // Add to the queue all successors with zero local in-degree
      for (auto Jump : Block.SuccJumps) {
        uint64_t Dst = Jump->Target;
        LocalInDegree[Dst]--;
        if (LocalInDegree[Dst] == 0) {
          Queue.push(Dst);
        }
      }
    }

    // If there is a cycle in the subgraph, AcyclicOrder contains only a subset
    // of all blocks
    if (DanglingSuccs.size() + 1 != AcyclicOrder.size())
      return false;
    DanglingSuccs = AcyclicOrder;
    return true;
  }

  void rebalanceDanglingSubgraph(FlowBlock *SrcBlock, FlowBlock *DstBlock,
                                 std::vector<FlowBlock *> &DanglingSuccs) {
    assert(SrcBlock->Flow > 0 && "zero-flow block in dangling subgraph");
    assert(DanglingSuccs.front() == SrcBlock && "incorrect order of dangles");

    for (auto Block : DanglingSuccs) {
      // Block's flow is the sum of incoming flows
      uint64_t TotalFlow = 0;
      if (Block == SrcBlock) {
        TotalFlow = Block->Flow;
      } else {
        for (auto Jump : Block->PredJumps) {
          TotalFlow += Jump->Flow;
        }
        Block->Flow = TotalFlow;
      }

      // Process all successor jumps and update corresponding flow values
      for (size_t I = 0; I < Block->SuccJumps.size(); I++) {
        auto Jump = Block->SuccJumps[I];
        if (I + 1 == Block->SuccJumps.size()) {
          Jump->Flow = TotalFlow;
          continue;
        }
        uint64_t Flow = uint64_t(TotalFlow * DanglingFirstSuccProbability);
        Jump->Flow = Flow;
        TotalFlow -= Flow;
      }
    }
  }

  /// A constant indicating an aribtrary exit block of a function.
  static constexpr uint64_t AnyExitBlock = uint64_t(-1);

  /// The function.
  FlowFunction &Func;

  /// If true, rebalance the flow going through dangling subgraphs.
  const bool RebalanceDangling;
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
  if (Func.Blocks[Func.Entry].Weight == 0) {
    Func.Blocks[Func.Entry].Weight = 1;
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
    assert((!Block.Dangling || Block.Weight == 0 || Block.isEntry()) &&
           "non-zero weight of a dangling block except for a dangling entry");

    // Split every block into two nodes
    uint64_t Bin = 3 * B;
    uint64_t Bout = 3 * B + 1;
    if (Block.Weight > 0) {
      Network.addEdge(S1, Bout, Block.Weight, 0);
      Network.addEdge(Bin, T1, Block.Weight, 0);
    }

    // Edges from S and to T
    assert((!Block.isEntry() || !Block.isExit()) &&
           "a block cannot be an entry and an exit");
    if (Block.isEntry()) {
      Network.addEdge(S, Bin, 0);
    } else if (Block.isExit()) {
      Network.addEdge(Bout, T, 0);
    }

    // An auxiliary node to allow increase/reduction of block counts:
    // We assume that decreasing block counts is more expensive than increasing,
    // and thus, setting separate costs here. In the future we may want to tune
    // the relative costs so as to maximize the quality of generated profiles.
    uint64_t Baux = 3 * B + 2;
    int64_t AuxCostInc = MinCostFlow::AuxCostInc;
    int64_t AuxCostDec = MinCostFlow::AuxCostDec;
    // Do not penalize changing weights of dangling blocks
    if (Block.Dangling) {
      AuxCostInc = 0;
      AuxCostDec = 0;
    }
    // Modifying the weight of the entry block is expensive unless it's dangling
    if (Block.isEntry() && !Block.Dangling) {
      AuxCostDec = MinCostFlow::AuxCostDecEntry;
      AuxCostInc = MinCostFlow::AuxCostIncEntry;
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
      uint64_t Cost = Jump.IsUnlikely ? MinCostFlow::AuxCostUnlikely : 0;
      Network.addEdge(SrcOut, DstIn, Cost);
    }
  }

  // Make sure we have a valid flow circulation
  Network.addEdge(T, S, 0);
}

/// Try to infer branch probabilities mimicking implementation of
/// BranchProbabilityInfo. Unlikely taken branches are marked so that the
/// inference algorithm can avoid sending flow along corresponding edges.
void findUnlikelyJumps(const std::vector<const BasicBlock *> &BasicBlocks,
                       BlockEdgeMap &Successors, FlowFunction &Func) {
  for (auto &Jump : Func.Jumps) {
    const auto *BB = BasicBlocks[Jump.Source];
    const auto *Succ = BasicBlocks[Jump.Target];
    const Instruction *TI = BB->getTerminator();
    // Check if a block ends with InvokeInst and mark non-taken branch unlikely.
    // In that case block Succ should be a landing pad
    if (Successors[BB].size() == 2 && Successors[BB].back() == Succ) {
      if (isa<InvokeInst>(TI)) {
        Jump.IsUnlikely = true;
      }
    }
    const Instruction *SuccTI = Succ->getTerminator();
    // Check if the target block contains UnreachableInst and mark it unlikely
    if (SuccTI->getNumSuccessors() == 0) {
      if (isa<UnreachableInst>(SuccTI)) {
        Jump.IsUnlikely = true;
      }
    }
  }
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
  const uint64_t NumBlocks = Func.Blocks.size();
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
    if (Block.isEntry()) {
      TotalInFlow += Block.Flow;
      assert(Block.Flow == OutFlow[I] && "incorrectly computed control flow");
    } else if (Block.isExit()) {
      TotalOutFlow += Block.Flow;
      assert(Block.Flow == InFlow[I] && "incorrectly computed control flow");
    } else {
      assert(Block.Flow == OutFlow[I] && "incorrectly computed control flow");
      assert(Block.Flow == InFlow[I] && "incorrectly computed control flow");
    }
  }
  assert(TotalInFlow == TotalOutFlow && "incorrectly computed control flow");

  // Verify that there are no isolated flow components
  // One could modify FlowFunction to hold edges indexed by the sources, which
  // will avoid a creation of the object
  auto PositiveFlowEdges = std::vector<std::vector<uint64_t>>(NumBlocks);
  for (auto &Jump : Func.Jumps) {
    if (Jump.Flow > 0) {
      PositiveFlowEdges[Jump.Source].push_back(Jump.Target);
    }
  }

  // Run bfs from the source along edges with positive flow
  std::queue<uint64_t> Queue;
  auto Visited = std::vector<bool>(NumBlocks, false);
  Queue.push(Func.Entry);
  Visited[Func.Entry] = true;
  while (!Queue.empty()) {
    uint64_t Src = Queue.front();
    Queue.pop();
    for (uint64_t Dst : PositiveFlowEdges[Src]) {
      if (!Visited[Dst]) {
        Queue.push(Dst);
        Visited[Dst] = true;
      }
    }
  }

  // Verify that every block that has a positive flow is reached from the source
  // along edges with a positive flow
  for (uint64_t I = 0; I < NumBlocks; I++) {
    auto &Block = Func.Blocks[I];
    assert((Visited[I] || Block.Flow == 0) && "an isolated flow component");
  }
}
#endif

} // end of anonymous namespace

/// Apply the profile inference algorithm for a given function
void SampleProfileInference::apply(BlockWeightMap &BlockWeights,
                                   EdgeWeightMap &EdgeWeights) {
  // Find all forwards reachable blocks which the inference algorithm will be
  // applied on.
  df_iterator_default_set<const BasicBlock *> Reachable;
  for (auto *BB : depth_first_ext(&F, Reachable))
    (void)BB /* Mark all reachable blocks */;

  // Find all backwards reachable blocks which the inference algorithm will be
  // applied on.
  df_iterator_default_set<const BasicBlock *> InverseReachable;
  for (const auto &BB : F) {
    // An exit block is a block without any successors.
    if (succ_empty(&BB)) {
      for (auto *RBB : inverse_depth_first_ext(&BB, InverseReachable))
        (void)RBB;
    }
  }

  // Keep a stable order for reachable blocks
  DenseMap<const BasicBlock *, uint64_t> BlockIndex;
  std::vector<const BasicBlock *> BasicBlocks;
  BlockIndex.reserve(Reachable.size());
  BasicBlocks.reserve(Reachable.size());
  for (const auto &BB : F) {
    if (Reachable.count(&BB) && InverseReachable.count(&BB)) {
      BlockIndex[&BB] = BasicBlocks.size();
      BasicBlocks.push_back(&BB);
    }
  }

  BlockWeights.clear();
  EdgeWeights.clear();
  bool HasSamples = false;
  for (const auto *BB : BasicBlocks) {
    auto It = SampleBlockWeights.find(BB);
    if (It != SampleBlockWeights.end() && It->second > 0) {
      HasSamples = true;
      BlockWeights[BB] = It->second;
    }
  }
  // Quit early for functions with a single block or ones w/o samples
  if (BasicBlocks.size() <= 1 || !HasSamples) {
    return;
  }

  // Create necessary objects
  FlowFunction Func;
  Func.Blocks.reserve(BasicBlocks.size());
  // Create FlowBlocks
  for (const auto *BB : BasicBlocks) {
    FlowBlock Block;
    if (SampleBlockWeights.find(BB) != SampleBlockWeights.end()) {
      Block.Dangling = false;
      Block.Weight = SampleBlockWeights[BB];
    } else {
      Block.Dangling = true;
      Block.Weight = 0;
    }
    Block.Index = Func.Blocks.size();
    Func.Blocks.push_back(Block);
  }
  // Create FlowEdges
  for (const auto *BB : BasicBlocks) {
    for (auto *Succ : Successors[BB]) {
      if (!BlockIndex.count(Succ))
        continue;
      FlowJump Jump;
      Jump.Source = BlockIndex[BB];
      Jump.Target = BlockIndex[Succ];
      Func.Jumps.push_back(Jump);
      if (BB == Succ) {
        Func.Blocks[BlockIndex[BB]].HasSelfEdge = true;
      }
    }
  }
  for (auto &Jump : Func.Jumps) {
    Func.Blocks[Jump.Source].SuccJumps.push_back(&Jump);
    Func.Blocks[Jump.Target].PredJumps.push_back(&Jump);
  }

  // Try to infer probabilities of jumps based on the content of basic block
  findUnlikelyJumps(BasicBlocks, Successors, Func);

  // Find the entry block
  for (size_t I = 0; I < Func.Blocks.size(); I++) {
    if (Func.Blocks[I].isEntry()) {
      Func.Entry = I;
      break;
    }
  }

  // Create and apply an inference network model
  auto InferenceNetwork = MinCostFlow();
  initializeNetwork(InferenceNetwork, Func);
  InferenceNetwork.run();

  // Extract flow values for every block and every edge
  extractWeights(InferenceNetwork, Func);

  // Post-MCF Adjustments to the flow
  auto Adjuster = FlowAdjuster(Func, RebalanceDanglingAfterMCF);
  Adjuster.run();

#ifndef NDEBUG
  // Verify the result
  verifyWeights(Func);
#endif

  // Extract the resulting weights from the control flow
  // All weights are increased by one to avoid propagation errors introduced by
  // zero weights.
  for (const auto *BB : BasicBlocks) {
    BlockWeights[BB] = Func.Blocks[BlockIndex[BB]].Flow;
  }
  for (auto &Jump : Func.Jumps) {
    Edge E = std::make_pair(BasicBlocks[Jump.Source], BasicBlocks[Jump.Target]);
    EdgeWeights[E] = Jump.Flow;
  }

#ifndef NDEBUG
  // Unreachable blocks and edges should not have a weight.
  for (auto &I : BlockWeights) {
    assert(Reachable.contains(I.first));
    assert(InverseReachable.contains(I.first));
  }
  for (auto &I : EdgeWeights) {
    assert(Reachable.contains(I.first.first) &&
           Reachable.contains(I.first.second));
    assert(InverseReachable.contains(I.first.first) &&
           InverseReachable.contains(I.first.second));
  }
#endif
}
