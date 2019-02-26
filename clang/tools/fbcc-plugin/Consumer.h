#pragma once

#include "Options.h"
#include "Result.h"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>

#include <memory>

namespace facebook {
namespace fbcc {
namespace plugin {

class Consumer : public clang::ASTConsumer,
                 public clang::RecursiveASTVisitor<Consumer> {
public:
  explicit Consumer(std::shared_ptr<Result> result);

  virtual ~Consumer() = default;

  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldWalkTypesOfTypeLocs() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }

  bool HandleTopLevelDecl(clang::DeclGroupRef dg) override;
  void HandleTranslationUnit(clang::ASTContext &context) override;

  bool VisitDecl(clang::Decl *d);
  bool VisitStmt(clang::Stmt *s);

  void finalize();

private:
  std::shared_ptr<Result> result;
};

} // namespace plugin
} // namespace fbcc
} // namespace facebook
