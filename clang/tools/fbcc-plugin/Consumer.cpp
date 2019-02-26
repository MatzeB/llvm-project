#include "Consumer.h"

#include <clang/Lex/Lexer.h>

using namespace clang;

namespace facebook {
namespace fbcc {
namespace plugin {

Consumer::Consumer(std::shared_ptr<Result> _result) : result(_result) {}

bool Consumer::HandleTopLevelDecl(DeclGroupRef dg) { return true; }

void Consumer::HandleTranslationUnit(ASTContext &context) {
  TraverseDecl(context.getTranslationUnitDecl());
  finalize();
}

bool Consumer::VisitDecl(Decl *d) {
  if (d) {
    ++result->DeclCount;
    ++result->DeclCountsPerKind[d->getKind()];
  }
  return true;
}

bool Consumer::VisitStmt(Stmt *s) {
  if (s) {
    ++result->StmtCount;
    ++result->StmtCountsPerKind[s->getStmtClass()];
  }
  return true;
}

void Consumer::finalize() {
  // Take ownership of result here.  We'll use it throughout this function and
  // finally release it when we return.
  auto resultSP = std::move(result);
  auto &R = *resultSP;

  if (R.opts.RawTokens) {
    auto const &SM = R.ci.getSourceManager();
    for (auto const FID : R.allFIDs) {
      // auto const FID = SM.getMainFileID();
      auto const buffer = SM.getBufferOrFake(FID);

      // "Raw" (i.e. without a preprocessor to pair with) lexer to stream the
      // tokens back to us.  TODO(steveo) maybe there's a way to intercept
      // the stream of tokens as the actual compile is happening, without having
      // to re-lex the whole thing.  Lexing is pretty fast, though, so meh.
      Lexer rawLexer(FID, buffer, SM, R.ci.getLangOpts());
      rawLexer.SetKeepWhitespaceMode(false);
      rawLexer.SetCommentRetentionState(false);

      size_t Counter = 0;
      Token t;
      while (true) {
        rawLexer.LexFromRawLexer(t);
        if (t.is(tok::eof)) {
          break;
        }
        ++Counter;
      }

      R.tokensPerFile[FID.getHashValue()] += Counter;
      R.tokens += Counter;
    }
  }

  R.dump();
}

} // namespace plugin
} // namespace fbcc
} // namespace facebook
