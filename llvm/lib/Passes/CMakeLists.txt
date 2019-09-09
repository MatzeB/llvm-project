add_llvm_component_library(LLVMPasses
  CFGChangeLogManager.cpp # facebook T53546053
  OptimizationLevel.cpp
  PassBuilder.cpp
  PassBuilderBindings.cpp
  PassBuilderPipelines.cpp
  PassPlugin.cpp
  StandardInstrumentations.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Passes

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  AggressiveInstCombine
  Analysis
  CodeGen #facebook T53546053
  Core
  Coroutines
  IPO
  InstCombine
  ObjCARC
  Scalar
  Support
  Target
  TransformUtils
  Vectorize
  Instrumentation
  )
