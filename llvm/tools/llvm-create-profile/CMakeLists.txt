# facebook T26943842
set(LLVM_LINK_COMPONENTS
        DebugInfoDWARF
        AllTargetsDescs
        Core
        AllTargetsInfos
        MC
        Object
        Support
        ProfileData
        Symbolize
        )
add_compile_options(-fno-rtti)

add_llvm_tool(llvm-create-profile
        ProfileCreator.cpp
        ProfileWriter.cpp
        SampleReader.cpp
        SymbolMap.cpp
        Profile.cpp
        LLVMProfileWriter.cpp
        llvm-create-profile.cpp)
