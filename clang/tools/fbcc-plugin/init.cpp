#include "Action.h"

#include <clang/Frontend/FrontendPluginRegistry.h>

using namespace clang;
using namespace llvm;

static FrontendPluginRegistry::Add<facebook::fbcc::plugin::Action> gFBCCPlugin(
    // use `-Xclang -plugin-arg-fbcc -Xclang foo` to pass `foo` to ParseArgs.
    "fbcc", "FBCCPlugin");
