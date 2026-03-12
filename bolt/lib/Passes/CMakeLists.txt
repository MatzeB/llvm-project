set(LLVM_BOLT_PASSES_SOURCES
  Aligner.cpp
  AllocCombiner.cpp
  AsmDump.cpp
  BinaryPasses.cpp
  CMOVConversion.cpp
  CacheMetrics.cpp
  CreateClonesAtOrigin.cpp
  DataflowAnalysis.cpp
  DataflowInfoManager.cpp
  FrameAnalysis.cpp
  FrameOptimizer.cpp
  FixRelaxationPass.cpp
  FixRISCVCallsPass.cpp
  HFSort.cpp
  Hugify.cpp
  IdenticalCodeFolding.cpp
  IndirectCallPromotion.cpp
  Inliner.cpp
  Instrumentation.cpp
  JTFootprintReduction.cpp
  LongJmp.cpp
  LoopInversionPass.cpp
  LivenessAnalysis.cpp
  MCF.cpp
  PatchEntries.cpp
  PAuthGadgetScanner.cpp
  PettisAndHansen.cpp
  PLTCall.cpp
  PointerAuthCFIAnalyzer.cpp
  PointerAuthCFIFixup.cpp
  ProfileQualityStats.cpp
  RegAnalysis.cpp
  RegReAssign.cpp
  ReorderAlgorithm.cpp
  ReorderFunctions.cpp
  ReorderData.cpp
  ShrinkWrapping.cpp
  SplitFunctions.cpp
  StackAllocationAnalysis.cpp
  StackAvailableExpressions.cpp
  StackPointerTracking.cpp
  StackReachingUses.cpp
  StokeInfo.cpp
  TailDuplication.cpp
  ThreeWayBranch.cpp
  ValidateInternalCalls.cpp
  ValidateMemRefs.cpp
  VeneerElimination.cpp
  RetpolineInsertion.cpp
  )

if (AArch64 IN_LIST BOLT_TARGETS_TO_BUILD)
  list(APPEND LLVM_BOLT_PASSES_SOURCES
    AArch64JumpTablePromotion.cpp
    AArch64RelaxationPass.cpp
    )
endif()

add_llvm_library(LLVMBOLTPasses
  ${LLVM_BOLT_PASSES_SOURCES}
  PARTIAL_SOURCES_INTENDED

  NO_EXPORT
  DISABLE_LLVM_LINK_LLVM_DYLIB

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}

  LINK_COMPONENTS
  AsmPrinter
  MC
  Support
  TargetParser
  TransformUtils
  )

target_link_libraries(LLVMBOLTPasses
  PRIVATE
  LLVMBOLTCore
  LLVMBOLTUtils
  )

if (AArch64 IN_LIST BOLT_TARGETS_TO_BUILD)
  target_include_directories(LLVMBOLTPasses PRIVATE
    ${LLVM_MAIN_SRC_DIR}/lib/Target/AArch64
    ${LLVM_BINARY_DIR}/lib/Target/AArch64
    )
endif()
