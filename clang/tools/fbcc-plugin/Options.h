#pragma once

#include <clang/Frontend/CompilerInstance.h>
#include <string>
#include <vector>

namespace facebook {
namespace fbcc {
namespace plugin {

struct Options {
  bool Enabled{true};
  std::string OutFile;
  bool Counts{false};
  bool CountsPerKind{false};
  bool Includes{false};
  bool RawTokens{false};

  void Parse(clang::CompilerInstance const &CI,
             std::vector<std::string> const &Args);

private:
  static bool boolVal(std::string const &val);

  void HandleArg(clang::CompilerInstance const &ci, std::string const &name,
                 std::string const &val);
};

} // namespace plugin
} // namespace fbcc
} // namespace facebook
