// facebook T21939124 D14256663
#include "Action.h"
#include "Consumer.h"
#include "Result.h"

#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>

using namespace clang;

namespace facebook {
namespace fbcc {
namespace plugin {

namespace {
class PluginCallbacks : public PPCallbacks {
public:
  explicit PluginCallbacks(std::shared_ptr<Result> _result) : result(_result) {}

  virtual ~PluginCallbacks() {}

  void InclusionDirective(SourceLocation hashLoc, Token const &includeTok,
                          StringRef fileName, bool isAngled,
                          CharSourceRange filenameRange,
                          llvm::Optional<FileEntryRef> File,
                          StringRef searchPath, StringRef relativePath,
                          Module const *imported,
                          SrcMgr::CharacteristicKind fileType) override;

  void FileChanged(clang::SourceLocation Loc, FileChangeReason Reason,
                   clang::SrcMgr::CharacteristicKind FileType,
                   clang::FileID PrevFID) override;

private:
  std::shared_ptr<Result> result;
};
} // namespace

void PluginCallbacks::InclusionDirective(
    SourceLocation hashLoc, Token const &includeTok, StringRef asWritten,
    bool isAngled, CharSourceRange /*filenameRange*/,
    llvm::Optional<FileEntryRef> fileEntry, StringRef /*searchPath*/,
    StringRef /*relativePath*/, Module const * /*imported*/,
    SrcMgr::CharacteristicKind /*FileType*/) {
  auto &fs = result->fileState;
  fs.onInclude(hashLoc, fileEntry, includeTok, std::string(asWritten),
               isAngled);
}

void PluginCallbacks::FileChanged(
    clang::SourceLocation Loc, FileChangeReason Reason,
    clang::SrcMgr::CharacteristicKind /*FileType*/, clang::FileID PrevFID) {
  auto &fs = result->fileState;
  auto const toFile = result->getSourceManager().getFileID(Loc);
  result->allFIDs.insert(toFile);
  switch (Reason) {
  case FileChangeReason::EnterFile: {
    fs.onEnterFile(toFile);
    break;
  }
  case FileChangeReason::ExitFile:
    fs.onExitFile();
    break;
  case FileChangeReason::RenameFile:
    fs.onFilenameChanged(result->ci.getSourceManager().getPresumedLoc(Loc));
    break;
  case FileChangeReason::SystemHeaderPragma:
    fs.onPragmaSystemHeader(toFile);
    break;
  }
}

std::unique_ptr<ASTConsumer> Action::CreateASTConsumer(CompilerInstance &ci,
                                                       llvm::StringRef inFile) {
  auto result = std::make_shared<Result>(ci, options_);
  if (options_.Includes) {
    auto &pp = result->ci.getPreprocessor();
    pp.addPPCallbacks(std::make_unique<PluginCallbacks>(result));
  }
  return std::make_unique<Consumer>(result);
}

bool Action::ParseArgs(CompilerInstance const &ci,
                       std::vector<std::string> const &args) {
  options_.Parse(ci, args);
  return true;
}

} // namespace plugin
} // namespace fbcc
} // namespace facebook
