set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  IRPrinter
  IRReader
  MC
  MIRParser
  Passes
  Remarks
  ScalarOpts
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils
  Vectorize
  Passes # facebook T53546053
  )

add_llvm_tool(llc
  llc.cpp
  NewPMDriver.cpp

  DEPENDS
  intrinsics_gen
  SUPPORT_PLUGINS
  )

export_executable_symbols_for_plugins(llc)
