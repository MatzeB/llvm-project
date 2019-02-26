#include "Options.h"

using namespace clang;

namespace facebook {
namespace fbcc {
namespace plugin {

bool Options::boolVal(std::string const &val) { return val == "1"; }

void Options::HandleArg(CompilerInstance const &ci, std::string const &name,
                        std::string const &val) {
  if (name == "Enabled") {
    Enabled = boolVal(val);
  } else if (name == "OutFile") {
    OutFile = val;
  } else if (name == "Counts") {
    Counts = boolVal(val);
  } else if (name == "CountsPerKind") {
    CountsPerKind = boolVal(val);
  } else if (name == "Includes") {
    Includes = boolVal(val);
  } else if (name == "RawTokens") {
    RawTokens = boolVal(val);
  } else {
    auto &diag = ci.getDiagnostics();
    auto diagID = diag.getCustomDiagID(DiagnosticsEngine::Warning,
                                       "ignoring invalid argument '%0'");
    diag.Report(diagID) << name;
  }
}

void Options::Parse(CompilerInstance const &CI,
                    std::vector<std::string> const &Args) {
  for (auto const &Arg : Args) {
    // Get the value part of "key=val".
    // If used without "=val", pretend it said "key=1".
    auto EqPos = Arg.find("=");
    std::string V;
    std::string K;
    if (EqPos == std::string::npos) {
      K = Arg;
      V = "1";
    } else {
      K = std::string(Arg.data(), EqPos);
      V = std::string(Arg.data() + EqPos + 1);
    }
    HandleArg(CI, K, V);
  }
}

} // namespace plugin
} // namespace fbcc
} // namespace facebook
