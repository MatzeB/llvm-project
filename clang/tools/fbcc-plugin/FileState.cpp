#include "FileState.h"
#include "Result.h"

using namespace clang;

namespace facebook {
namespace fbcc {
namespace plugin {

FileRecord::FileRecord(size_t _index, clang::FileEntry const *_thisFileEntry)
    : index(_index), thisFileEntry(_thisFileEntry) {}

llvm::StringRef FileRecord::thisFilePath() const {
  return this->thisFileEntry->getName();
}

FileState::FileState(Result *_result) : result(_result) {
  // Important: main file is the first file added to this state object.
  auto &mainRecord = addFile(getMainFileEntry());
  mainRecord.isMainFile = true;
  // Initially the stack contains only the main file.
  stack.push(mainRecord.index);
}

clang::CompilerInstance const &FileState::getCompilerInstance() const {
  return result->getCompilerInstance();
}

clang::SourceManager const &FileState::getSourceManager() const {
  return result->getSourceManager();
}

FileID const FileState::getMainFileID() const {
  auto const &sm = result->ci.getSourceManager();
  return sm.getMainFileID();
}

FileEntry const *FileState::getMainFileEntry() const {
  auto const &sm = result->ci.getSourceManager();
  return sm.getFileEntryForID(getMainFileID());
}

FileRecord &FileState::addFile(FileEntry const *fileEntry) {
  auto const index = records.size();
  records.emplace_back(std::make_unique<FileRecord>(index, fileEntry));
  return *records.back();
}

FileEntry const *FileState::fileEntryOf(FileID FID) const {
  auto const &sm = result->ci.getSourceManager();
  return sm.getFileEntryForID(FID);
}

FileEntry const *FileState::fileEntryOf(SourceLocation Loc) const {
  auto const &sm = result->ci.getSourceManager();
  return fileEntryOf(sm.getFileID(Loc));
}

void FileState::onInclude(SourceLocation IncludeLoc,
                          FileEntry const *IncludedFileEntry,
                          Token const & /*IncludeTok*/,
                          std::string const &asWritten, bool IsAngled) {
  auto const &parentRecord = tos();
  auto &record = addFile(IncludedFileEntry);
  record.inclusionLoc = IncludeLoc;
  record.depth = parentRecord.depth + 1;
  record.parentIndex = parentRecord.index;
  record.asWritten = asWritten;
  record.isSystemHeader = IsAngled || parentRecord.isSystemHeader;
}

void FileState::onPragmaSystemHeader(FileID const) {
  tos().isSystemHeader = true;
}

void FileState::onExitFile() { stack.pop(); }

void FileState::onEnterFile(FileID const enteringFileID) {
  auto &record = *records.back();
  record.thisFileID = enteringFileID;
  record.didEnterFile = true;
  stack.push(record.index);
}

void FileState::onFilenameChanged(PresumedLoc const presumedLoc) {
  auto &prevRecord = tos();
  auto const *presumedFilename = presumedLoc.getFilename();

  auto &record = addFile(FileRecord::kInvalidFileEntry);
  record.depth = prevRecord.depth;
  record.isMainFile = prevRecord.isMainFile;
  record.parentIndex = prevRecord.index;
  record.asWritten = presumedFilename;
  record.didEnterFile = true;
  record.isSystemHeader = prevRecord.isSystemHeader;
  record.isFilenameChangeOnly = true;

  stack.pop();
  stack.push(record.index);
}

} // namespace plugin
} // namespace fbcc
} // namespace facebook
