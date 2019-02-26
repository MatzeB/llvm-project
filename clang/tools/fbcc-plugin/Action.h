#pragma once

#include "Options.h"

#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>

#include <memory>
#include <string>
#include <vector>

namespace facebook {
namespace fbcc {
namespace plugin {

class Action : public clang::PluginASTAction {
public:
  virtual ~Action() = default;

  clang::PluginASTAction::ActionType getActionType() override {
    return clang::PluginASTAction::ActionType::AddAfterMainAction;
  }

  bool usesPreprocessorOnly() const override final { return false; }
  bool hasPCHSupport() const override final { return true; }
  bool hasASTFileSupport() const override final { return true; }
  bool hasIRSupport() const override final { return false; }
  bool hasCodeCompletionSupport() const override final { return false; }
  bool isModelParsingAction() const override final { return false; }

protected:
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci,
                    llvm::StringRef inFile) override;

  bool ParseArgs(clang::CompilerInstance const &ci,
                 std::vector<std::string> const &args) override;

private:
  Options options_;
};

} // namespace plugin
} // namespace fbcc
} // namespace facebook
