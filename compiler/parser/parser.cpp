#include "util/fmt/format.h"
#include <iostream>
#include <string>
#include <vector>

#include "lang/seq.h"
#include "parser/ast/codegen/stmt.h"
#include "parser/ast/format/stmt.h"
#include "parser/ast/transform/stmt.h"
#include "parser/context.h"
#include "parser/ocaml.h"
#include "parser/parser.h"

using std::make_shared;
using std::string;
using std::vector;

namespace seq {

seq::SeqModule *parse(const std::string &argv0, const std::string &file,
                      bool isCode, bool isTest) {
  try {
    auto stmts = isCode ? ast::parse_code(argv0, file) : ast::parse_file(file);
    auto tv = ast::TransformStmtVisitor::apply(move(stmts));

    auto module = new seq::SeqModule();
    auto cache = ast::ImportCache{string(argv0), nullptr, {}};
    auto stdlib = make_shared<ast::Context>(module, cache);
    auto context = make_shared<ast::Context>(module, cache, file);
    ast::CodegenStmtVisitor::apply(*context, tv);
    return context->getModule();
  } catch (seq::exc::SeqException &e) {
    if (isTest) {
      throw;
    }
    seq::compilationError(e.what(), e.getSrcInfo().file, e.getSrcInfo().line,
                          e.getSrcInfo().col);
    return nullptr;
  }
}

void execute(seq::SeqModule *module, vector<string> args, vector<string> libs,
             bool debug) {
  try {
    module->execute(args, libs, debug);
  } catch (exc::SeqException &e) {
    compilationError(e.what(), e.getSrcInfo().file, e.getSrcInfo().line,
                     e.getSrcInfo().col);
  }
}

void compile(seq::SeqModule *module, const string &out, bool debug) {
  try {
    module->compile(out, debug);
  } catch (exc::SeqException &e) {
    compilationError(e.what(), e.getSrcInfo().file, e.getSrcInfo().line,
                     e.getSrcInfo().col);
  }
}

} // namespace seq