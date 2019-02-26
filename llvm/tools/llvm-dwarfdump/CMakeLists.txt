set(LLVM_LINK_COMPONENTS
  DebugInfoDWARF
  AllTargetsDescs
  AllTargetsInfos
  MC
  Object
  Support
  )

add_llvm_tool(llvm-dwarfdump
  SectionSizes.cpp
  Analyze.cpp # facebook D13311561
  Statistics.cpp
  llvm-dwarfdump.cpp
  )

add_subdirectory(fuzzer)
