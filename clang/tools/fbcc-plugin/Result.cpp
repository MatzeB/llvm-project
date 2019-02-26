#include "Result.h"

#include <system_error>

using namespace clang;

namespace facebook {
namespace fbcc {
namespace plugin {

Result::Result(CompilerInstance const &_ci, Options const &_opts)
    : ci(_ci), opts(_opts), FileOS(newOutputStream(opts)), fileState(this) {}

char const *Result::getDeclKindName(unsigned kind) const {
  static auto const table = buildDeclNameTable();
  auto const *name = table[kind];
  return name;
}

char const *Result::getStmtKindName(unsigned kind) const {
  static auto const table = buildStmtNameTable();
  auto const *name = table[kind];
  return name;
}

std::unique_ptr<llvm::raw_fd_ostream>
Result::newOutputStream(Options const &opts) {
  if (!opts.OutFile.empty()) {
    std::error_code EC;
    std::unique_ptr<llvm::raw_fd_ostream> FileOS;
    FileOS.reset(
        new llvm::raw_fd_ostream(opts.OutFile, EC, llvm::sys::fs::OF_None));
    if (EC) {
      llvm::errs() << "could not open file for writing: " << EC.message()
                   << ": " << opts.OutFile << '\n';
    } else {
      FileOS->SetUnbuffered();
      return FileOS;
    }
  }

  return nullptr;
}

Result::DeclKindTable<char const *> Result::buildDeclNameTable() {
  static DeclKindTable<char const *> names;
#define STRINGIFY0(x) #x
#define STRINGIFY(x) STRINGIFY0(x)
#define ABSTRACT_DECL(DECL)
#define DECL(CLASS, BASE) names[Decl::Kind::CLASS] = STRINGIFY(CLASS##Decl);
#include "clang/AST/DeclNodes.inc" // note: undefines [ABSTRACT_]DECL for us
#undef STRINGIFY
#undef STRINGIFY0
  return names;
}

Result::StmtKindTable<char const *> Result::buildStmtNameTable() {
  StmtKindTable<char const *> names;
#define STRINGIFY0(x) #x
#define STRINGIFY(x) STRINGIFY0(x)
#define ABSTRACT_STMT(STMT)
#define STMT(CLASS, BASE)                                                      \
  names[Stmt::StmtClass::CLASS##Class] = STRINGIFY(CLASS);
#include "clang/AST/StmtNodes.inc" // note: undefines [ABSTRACT_]STMT for us
#undef STRINGIFY
#undef STRINGIFY0
  return names;
}

/// Output format: one record per line, fields separated by tabs.  Fields are:
/// 1: the string `Counts` to identify this type of record (corresponding to
///    the `Options` field of the same name);
/// 2: the string `decl` to indicate this is for Decl types;
/// 3: the Decl class name, if `CountsPerKind` option enabled.  There's one
///    decl record having a string `*` in this position, representing the total
///    number of all decls;
/// 4: the count.
void Result::dumpDecls(llvm::raw_ostream &OS) const {
  OS << "Counts\tdecl\t*\t" << DeclCount << '\n';
  if (opts.CountsPerKind) {
    for (unsigned kind = 0; kind < DeclCountsPerKind.size(); kind++) {
      auto const count = DeclCountsPerKind[kind];
      if (count) {
        auto const *name = getDeclKindName(kind);
        OS << "Counts\tdecl\t" << name << "\t" << count << '\n';
      }
    }
  }
}

/// Output format: one record per line, fields separated by tabs.  Fields are:
/// 1: the string `Counts` to identify this type of record (corresponding to
///    the `Options` field of the same name);
/// 2: the string `stmt` to indicate this is for Stmt types;
/// 3: the Stmt class name, if `CountsPerKind` option enabled.  There's one
///    stmt record having a string `*` in this position, representing the total
///    number of all stmts;
/// 4: the count.
void Result::dumpStmts(llvm::raw_ostream &OS) const {
  OS << "Counts\tstmt\t*\t" << StmtCount << '\n';
  if (opts.CountsPerKind) {
    for (unsigned kind = 0; kind < StmtCountsPerKind.size(); kind++) {
      auto const count = StmtCountsPerKind[kind];
      if (count) {
        auto const *name = getStmtKindName(kind);
        OS << "Counts\tstmt\t" << name << "\t" << count << '\n';
      }
    }
  }
}

/// Output format: one record per line, fields separated by tabs.  Fields are:
/// 1: the string `Includes` to identify this type of record (corresponding to
///    the `Options` field of the same name);
/// 2: the string `FileRecord` for this record's subtype;
/// 3: number: index of this record, main .cpp file is at index 0;
/// 4: number: type (see `enum class FileRecordType`);
/// 5: number: depth of this inclusion, main file has 0;
/// 6: number: parent: includer, or previous entry if this is a filename change;
/// 7: number: 0 or 1 to indicate whether file was entered (1) or skipped (0);
/// 8: string: filename.
void Result::dumpIncludes(llvm::raw_ostream &OS) const {
  for (auto const &rec : fileState.getRecords()) {
    OS << "Includes"
       << "\tFileRecord" << '\t' << rec->index << '\t' << rec->depth << '\t'
       << rec->parentIndex << '\t' << int(rec->didEnterFile) << '\t'
       << int(rec->isMainFile) << '\t' << int(rec->isSystemHeader) << '\t'
       << int(rec->isFilenameChangeOnly) << '\t'
       << int(rec->thisFileID.getHashValue()) << '\t'
       << rec->getThisFileDisplayName() << '\n';
  }
}

/// Output format:
/// 1: string `Preprocessed`
/// 2: string `RawTokens`
/// 3: int: FileID number, except one row has 0 for the whole TU.
///    (Zero indicates an invalid file ID, thus it's unused for real file
///    entries.  Since it's unused, we'll assign to it this new meaning.)
/// 4: number of tokens from preprocessed input
void Result::dumpRawTokens(llvm::raw_ostream &OS) const {
  OS << "Preprocessed\tRawTokens\t0\t" << tokens << '\n';
  for (auto const &entry : tokensPerFile) {
    auto const fileID = entry.first;
    auto const count = entry.second;
    // Note: fileID is not `clang::FileID`, but instead the opaque number it
    // wraps -- simply an `unsigned` which is returned by `ID.getHashValue()`.
    // The "hash value" is the same as the ID, not modified nor hashed.
    // A nonzero ID number indicates the FileID was valid.  See definition
    // in: llvm/tools/clang/include/clang/Basic/SourceLocation.h
    if (fileID) {
      OS << "Preprocessed\tRawTokens\t" << fileID << '\t' << count << '\n';
    }
  }
}

void Result::dump() const {
  auto &OS = FileOS ? *FileOS : llvm::errs();
  if (opts.Counts) {
    dumpDecls(OS);
    dumpStmts(OS);
  }
  if (opts.Includes) {
    dumpIncludes(OS);
  }
  if (opts.RawTokens) {
    dumpRawTokens(OS);
  }
}

} // namespace plugin
} // namespace fbcc
} // namespace facebook
