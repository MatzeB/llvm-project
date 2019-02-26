#pragma once

#include "FileState.h"
#include "Options.h"

#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <memory>
#include <set>
#include <unordered_map>

namespace facebook {
namespace fbcc {
namespace plugin {

class Result {
public:
  static constexpr unsigned const kNumDeclKinds =
      1u + clang::Decl::Kind::lastDecl;

  static constexpr unsigned const kNumStmtKinds =
      1u + clang::Stmt::StmtClass::lastStmtConstant;

  template <typename T> using DeclKindTable = std::array<T, kNumDeclKinds>;

  template <typename T> using StmtKindTable = std::array<T, kNumStmtKinds>;

  clang::CompilerInstance const &ci;
  Options const opts;
  std::unique_ptr<llvm::raw_fd_ostream> const FileOS;

  unsigned DeclCount{0};
  DeclKindTable<unsigned> DeclCountsPerKind{};

  unsigned StmtCount{0};
  StmtKindTable<unsigned> StmtCountsPerKind{};

  FileState fileState;
  std::set<clang::FileID> allFIDs;

  size_t tokens{0};
  std::unordered_map<unsigned, size_t> tokensPerFile;

  clang::CompilerInstance const &getCompilerInstance() const { return ci; }

  clang::SourceManager const &getSourceManager() const {
    return ci.getSourceManager();
  }

  Result(clang::CompilerInstance const &_ci, Options const &_opts);

  /// Get a name for decls of this kind.
  /// This matches the unqualified name of the \c Decl subclass.
  /// E.g. \c TypedefDecl
  char const *getDeclKindName(unsigned kind) const;

  /// Get a name for stmts of this kind.
  /// This matches the unqualified name of the \c Stmt subclass.
  /// E.g. \c IntegerLiteral
  char const *getStmtKindName(unsigned kind) const;

  void dump() const;

private:
  /// Get an output stream to write collected data to. If there's no \b OutFile
  /// specified in options, or there was an error creating the file, then this
  /// will be empty.
  static std::unique_ptr<llvm::raw_fd_ostream>
  newOutputStream(Options const &opts);

  static DeclKindTable<char const *> buildDeclNameTable();
  static StmtKindTable<char const *> buildStmtNameTable();

  void dumpDecls(llvm::raw_ostream &OS) const;
  void dumpStmts(llvm::raw_ostream &OS) const;
  void dumpIncludes(llvm::raw_ostream &OS) const;
  void dumpRawTokens(llvm::raw_ostream &OS) const;
};

} // namespace plugin
} // namespace fbcc
} // namespace facebook
