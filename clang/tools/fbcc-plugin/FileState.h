#pragma once

#include <clang/Basic/FileManager.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>

#include <cstdint>
#include <stack>
#include <unordered_map>
#include <unordered_set>

namespace facebook {
namespace fbcc {
namespace plugin {

class FileState;
class Result;

struct FileRecord {
  static constexpr clang::FileID const kInvalidFileID{};

  /// 0-based index.  Zero is for the main file.  The main file can have other
  /// entries after index 0 as well: that's the case for `isFilenameChangeOnly`
  /// if used within the main file.
  size_t const index;
  /// The \b FileEntry object which \b thisFileID represents.  Note that there
  /// may be more than one \b FileID per \b FileEntry, but not the other way
  /// around.  The \b FileEntry tends to correspond 1:1 with physical files.
  llvm::Optional<clang::FileEntryRef> thisFileEntry;

  /// Non-const members which are adjusted by \b FileState class.

  /// The FileEntry for the file.  This can be null (\c kInvalidFileID) if not a
  /// real file (macro expansion scratch space, a `isFilenameChangeOnly` file,
  /// perhaps other cases).
  clang::FileID thisFileID{kInvalidFileID};
  /// Inclusions only: SourceLocation of where the inclusion of this file
  /// occurred, within the parent file.
  clang::SourceLocation inclusionLoc;
  /// How deeply nested this file is as a result of inclusions; main file is 0.
  unsigned depth{0};
  /// Includer, or record for same file prior to FILENAME_CHANGE entry.
  size_t parentIndex{0};
  /// Filename inside the quotes/angle-brackets of the #include, or the filename
  /// given in a #line directive.  Never \b null.
  llvm::StringRef asWritten;
  /// Main file?  True for the main file (entry at index zero), and propagates
  /// to any entries which are filename changes within this main file.
  bool isMainFile{false};
  /// Was the file entered?  Initially false, but flips to true in `onEnterFile`
  /// callback.  This is to detect inclusions that didn't actually occur due to
  /// include guard macros and `#pragma once`.
  bool didEnterFile{false};
  /// True if included with angle brackets, or header uses #pragma GCC
  /// system_header; or, if the parent was a system header.  This value
  /// propagates to all kinds of child entries (even those file brought in with
  /// quoted includes).
  bool isSystemHeader{false};
  /// Not a file transition, but only a name change, e.g. with #line directive.
  /// Such entries still inherit `isMainFile`.
  bool isFilenameChangeOnly{false};

  FileRecord(size_t index, llvm::Optional<clang::FileEntryRef> entry);

  /// If this is a real file, i.e. \c thisFile != \c kInvalidFileID, this is
  /// the true path name; otherwise a null string.
  llvm::StringRef thisFilePath() const;

  llvm::StringRef getThisFileDisplayName() const {
    return isRealFile() ? thisFilePath() : asWritten;
  }

  /// Is an actual file on the filesystem, or something else like
  /// <built-in> or scratch space.
  bool isRealFile() const { return thisFileEntry != llvm::None; }
};

class FileState {
public:
  explicit FileState(Result *result);

  clang::CompilerInstance const &getCompilerInstance() const;

  clang::SourceManager const &getSourceManager() const;

  /// Add file to internal map (does nothing if already added).
  /// Returns reference to file's corresponding new / existing FileRecord.
  FileRecord &addFile(llvm::Optional<clang::FileEntryRef> FID);

  std::vector<std::unique_ptr<FileRecord>> const &getRecords() const {
    return records;
  }

  FileRecord const *getRecord(size_t index) const {
    return index < records.size() ? &*records[index] : nullptr;
  }

  clang::FileID const getMainFileID() const;
  llvm::Optional<clang::FileEntryRef> getMainFileEntry() const;
  llvm::Optional<clang::FileEntryRef> fileEntryOf(clang::FileID FID) const;
  llvm::Optional<clang::FileEntryRef>
  fileEntryOf(clang::SourceLocation Loc) const;
  llvm::Optional<clang::FileEntryRef> fileEntryOf(clang::PresumedLoc Loc) const;

  /// An \b #include was encountered, so Clang is calling back to us.  This
  /// occurs whether the inclusion actually happens or not, i.e. \b onEnterFile
  /// may or may not actually be called.  Don't assume we're entering the file
  /// just because this is called.
  void onInclude(clang::SourceLocation IncludeLoc,
                 llvm::Optional<clang::FileEntryRef> IncludedFileEntry,
                 clang::Token const &IncludeToken,
                 std::string const &nameAsWritten, bool IsAngled);

  /// Occurs when we transition from one file into another.  Expects that
  /// \b onInclude was called prior to this being called.
  void onEnterFile(clang::FileID const enteringFileID);

  /// Changing the filename -- but not file! -- via a \b #line directive.
  void onFilenameChanged(clang::PresumedLoc const presumedLoc);

  /// Returning from an included file back to its includer.
  void onExitFile();

  /// Called upon seeing a `#pragma GCC system_header`.
  void onPragmaSystemHeader(clang::FileID const fileID);

private:
  FileRecord &tos() { return *records.at(stack.top()); }

  /// The \b Result instance which owns \b this.
  Result *result;

  /// List of file records in the order in which we encounter them.
  /// Invariants:
  /// This is never empty, only grows, and entry 0 is the main file.
  std::vector<std::unique_ptr<FileRecord>> records;

  /// Tracks the current file nesting state.
  /// Invariants:
  /// Never empty until the preprocessor completes.
  /// The bottom of the stack corresponds to the main file.
  /// TOS always corresponds to the "current" file.
  std::stack<size_t> stack;
};

} // namespace plugin
} // namespace fbcc
} // namespace facebook
