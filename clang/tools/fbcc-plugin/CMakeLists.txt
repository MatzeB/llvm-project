# facebook T21939124

add_llvm_library(FBCCPlugin MODULE
    init.cpp

    Action.cpp
    Consumer.cpp
    FileState.cpp
    Options.cpp
    Result.cpp

    PLUGIN_TOOL
    clang

    DEPENDS
    omp_gen
    )
