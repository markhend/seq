#include "format.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

#include "util/fmt/format.h"
#include "util/fmt/ostream.h"
#include "visitor.h"

namespace {
using namespace seq::ir;

struct NodeFormatter {
  const types::Type *type = nullptr;
  const Value *value = nullptr;
  const Var *var = nullptr;
  bool canShowFull = false;

  std::unordered_set<int> &seenNodes;
  std::unordered_set<std::string> &seenTypes;

  NodeFormatter(const types::Type *type, std::unordered_set<int> &seenNodes,
                std::unordered_set<std::string> &seenTypes)
      : type(type), seenNodes(seenNodes), seenTypes(seenTypes) {}

  NodeFormatter(const Value *value, std::unordered_set<int> &seenNodes,
                std::unordered_set<std::string> &seenTypes)
      : value(value), seenNodes(seenNodes), seenTypes(seenTypes) {}
  NodeFormatter(const Var *var, std::unordered_set<int> &seenNodes,
                std::unordered_set<std::string> &seenTypes)
      : var(var), seenNodes(seenNodes), seenTypes(seenTypes) {}

  friend std::ostream &operator<<(std::ostream &os, const NodeFormatter &n);
};

class FormatVisitor : util::ConstVisitor {
private:
  std::ostream &os;
  std::unordered_set<int> &seenNodes;
  std::unordered_set<std::string> &seenTypes;

public:
  FormatVisitor(std::ostream &os, std::unordered_set<int> &seenNodes,
                std::unordered_set<std::string> &seenTypes)
      : os(os), seenNodes(seenNodes), seenTypes(seenTypes) {}
  virtual ~FormatVisitor() noexcept = default;

  void visit(const Module *v) override {
    auto types = makeFormatters(v->types_begin(), v->types_end(), true);
    auto vars = makeFormatters(v->begin(), v->end(), true);
    fmt::print(os, FMT_STRING("(module\n(argv {})\n(types {})\n(vars {})\n{})"),
               makeFormatter(v->getArgVar(), true),
               fmt::join(types.begin(), types.end(), "\n"),
               fmt::join(vars.begin(), vars.end(), "\n"),
               makeFormatter(v->getMainFunc(), true));
  }

  void visit(const Var *v) override {
    fmt::print(os, FMT_STRING("(var '\"{}\" {} (global {}))"), v->referenceString(),
               makeFormatter(v->getType()), v->isGlobal());
  }

  void defaultVisit(const Func *) override { os << "(unknown_func)"; }
  void visit(const BodiedFunc *v) override {
    auto args = makeFormatters(v->arg_begin(), v->arg_end(), true);
    auto symbols = makeFormatters(v->begin(), v->end(), true);
    fmt::print(os, FMT_STRING("(bodied_func '\"{}\" {}\n(args {})\n(vars {})\n{})"),
               v->referenceString(), makeFormatter(v->getType()),
               fmt::join(args.begin(), args.end(), " "),
               fmt::join(symbols.begin(), symbols.end(), " "),
               makeFormatter(v->getBody()));
  }
  void visit(const ExternalFunc *v) override {
    fmt::print(os, FMT_STRING("(external_func '\"{}\" {})"), v->referenceString(),
               makeFormatter(v->getType()));
  }
  void visit(const InternalFunc *v) override {
    fmt::print(os, FMT_STRING("(internal_func '\"{}\" {})"), v->referenceString(),
               makeFormatter(v->getType()));
  }
  void visit(const LLVMFunc *v) override {
    std::vector<std::string> literals;

    for (auto it = v->literal_begin(); it != v->literal_end(); ++it) {
      const auto &l = *it;
      if (l.isStatic()) {
        literals.push_back(fmt::format(FMT_STRING("(static {})"), l.getStaticValue()));
      } else {
        literals.push_back(
            fmt::format(FMT_STRING("(type {})"), makeFormatter(l.getTypeValue())));
      }
    }

    auto body = v->getLLVMBody();
    std::replace(body.begin(), body.end(), '\n', ' ');

    fmt::print(os,
               FMT_STRING("(llvm_func '\"{}\" {}\n(decls \"{}\")\n"
                          "\"{}\"\n(literals ({})))"),
               v->referenceString(), makeFormatter(v->getType()),
               v->getLLVMDeclarations(), body,
               fmt::join(literals.begin(), literals.end(), "\n"));
  }

  void defaultVisit(const Value *) override { os << "(unknown_value)"; }
  void visit(const VarValue *v) override {
    fmt::print(os, FMT_STRING("'\"{}\""), v->getVar()->referenceString());
  }
  void visit(const PointerValue *v) override {
    fmt::print(os, FMT_STRING("(ptr '\"{}\")"), v->getVar()->referenceString());
  }

  void defaultVisit(const Flow *) override { os << "(unknown_flow)"; }
  void visit(const SeriesFlow *v) override {
    auto series = makeFormatters(v->begin(), v->end());
    fmt::print(os, FMT_STRING("(series\n{}\n)"),
               fmt::join(series.begin(), series.end(), "\n"));
  }
  void visit(const IfFlow *v) override {
    fmt::print(os, FMT_STRING("(if {}\n{}\n{}\n)"), makeFormatter(v->getCond()),
               makeFormatter(v->getTrueBranch()), makeFormatter(v->getFalseBranch()));
  }
  void visit(const WhileFlow *v) override {
    fmt::print(os, FMT_STRING("(while {}\n{}\n)"), makeFormatter(v->getCond()),
               makeFormatter(v->getBody()));
  }
  void visit(const ForFlow *v) override {
    fmt::print(os, FMT_STRING("(for {}\n{}\n{}\n)"), makeFormatter(v->getIter()),
               makeFormatter(v->getVar()), makeFormatter(v->getBody()));
  }
  void visit(const TryCatchFlow *v) override {
    std::vector<std::string> catches;

    for (auto &c : *v) {
      catches.push_back(
          fmt::format(FMT_STRING("(catch {} {}\n{}\n)"), makeFormatter(c.getType()),
                      makeFormatter(c.getVar()), makeFormatter(c.getHandler())));
    }

    fmt::print(os, FMT_STRING("(try {}\n{}\n(finally\n{}\n)\n)"),
               makeFormatter(v->getBody()),
               fmt::join(catches.begin(), catches.end(), "\n"),
               makeFormatter(v->getFinally()));
  }
  void visit(const PipelineFlow *v) override {
    std::vector<std::string> stages;
    for (const auto &s : *v) {
      auto args = makeFormatters(s.begin(), s.end());
      stages.push_back(fmt::format(
          FMT_STRING("(stage {} {}\n(generator {})\n(parallel {}))"),
          makeFormatter(s.getCallee()), fmt::join(args.begin(), args.end(), "\n"),
          s.isGenerator(), s.isParallel()));
    }
    fmt::print(os, FMT_STRING("(pipeline {})"),
               fmt::join(stages.begin(), stages.end(), "\n"));
  }
  void visit(const dsl::CustomFlow *v) override { v->doFormat(os); }

  void defaultVisit(const Const *) override { os << "unknown_const"; }
  void visit(const IntConst *v) override {
    fmt::print(os, FMT_STRING("{}"), v->getVal());
  }
  void visit(const FloatConst *v) override {
    fmt::print(os, FMT_STRING("{}"), v->getVal());
  }
  void visit(const BoolConst *v) override {
    fmt::print(os, FMT_STRING("{}"), v->getVal());
  }
  void visit(const StringConst *v) override {
    std::stringstream escaped;
    for (char c : v->getVal()) {
      switch (c) {
      case '\a':
        escaped << "\\a";
        break;
      case '\b':
        escaped << "\\b";
        break;
      case '\f':
        escaped << "\\f";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      case '\v':
        escaped << "\\v";
        break;
      case '\\':
        escaped << "\\\\";
        break;
      case '\'':
        escaped << "\\'";
        break;
      case '\"':
        escaped << "\\\"";
        break;
      case '\?':
        escaped << "\\\?";
        break;
      default:
        escaped << c;
      }
    }
    fmt::print(os, FMT_STRING("\"{}\""), escaped.str());
  }
  void visit(const dsl::CustomConst *v) override { v->doFormat(os); }

  void defaultVisit(const Instr *) override { os << "(unknown_instr)"; }
  void visit(const AssignInstr *v) override {
    fmt::print(os, FMT_STRING("(assign {} {})"), makeFormatter(v->getLhs()),
               makeFormatter(v->getRhs()));
  }
  void visit(const ExtractInstr *v) override {
    fmt::print(os, FMT_STRING("(extract {} \"{}\")"), makeFormatter(v->getVal()),
               v->getField());
  }
  void visit(const InsertInstr *v) override {
    fmt::print(os, FMT_STRING("(insert {} \"{}\" {})"), makeFormatter(v->getLhs()),
               v->getField(), makeFormatter(v->getRhs()));
  }
  void visit(const CallInstr *v) override {
    auto args = makeFormatters(v->begin(), v->end());
    fmt::print(os, FMT_STRING("(call {}\n{}\n)"), makeFormatter(v->getCallee()),
               fmt::join(args.begin(), args.end(), "\n"));
  }
  void visit(const StackAllocInstr *v) override {
    fmt::print(os, FMT_STRING("(stack_alloc {} {})"), makeFormatter(v->getArrayType()),
               v->getCount());
  }
  void visit(const TypePropertyInstr *v) override {
    std::string property;
    if (v->getProperty() == TypePropertyInstr::Property::IS_ATOMIC) {
      property = "atomic";
    } else if (v->getProperty() == TypePropertyInstr::Property::SIZEOF) {
      property = "sizeof";
    } else {
      property = "unknown";
    }
    fmt::print(os, FMT_STRING("(property {} {})"), property,
               makeFormatter(v->getInspectType()));
  }
  void visit(const YieldInInstr *v) override {
    fmt::print(os, FMT_STRING("(yield_in {})"), makeFormatter(v->getType()));
  }
  void visit(const TernaryInstr *v) override {
    fmt::print(os, FMT_STRING("(select {}\n{}\n{}\n)"), makeFormatter(v->getCond()),
               makeFormatter(v->getTrueValue()), makeFormatter(v->getFalseValue()));
  }
  void visit(const BreakInstr *v) override { os << "break"; }
  void visit(const ContinueInstr *v) override { os << "continue"; }
  void visit(const ReturnInstr *v) override {
    fmt::print(os, FMT_STRING("(return {})"), makeFormatter(v->getValue()));
  }
  void visit(const YieldInstr *v) override {
    fmt::print(os, FMT_STRING("(yield {})"), makeFormatter(v->getValue()));
  }
  void visit(const ThrowInstr *v) override {
    fmt::print(os, FMT_STRING("(throw {})"), makeFormatter(v->getValue()));
  }
  void visit(const FlowInstr *v) override {
    fmt::print(os, FMT_STRING("(flow {} {})"), makeFormatter(v->getFlow()),
               makeFormatter(v->getValue()));
  }
  void visit(const dsl::CustomInstr *v) override { v->doFormat(os); }

  void defaultVisit(const types::Type *) override { os << "unknown_type"; }
  void visit(const types::IntType *v) override { os << "int"; }
  void visit(const types::FloatType *v) override { os << "float"; }
  void visit(const types::BoolType *v) override { os << "bool"; }
  void visit(const types::ByteType *v) override { os << "byte"; }
  void visit(const types::VoidType *v) override { os << "void"; }
  void visit(const types::RecordType *v) override {
    std::vector<std::string> fields;
    std::vector<NodeFormatter> formatters;
    for (const auto &m : *v) {
      fields.push_back(fmt::format(FMT_STRING("(\"{}\" {})"), m.getName(),
                                   makeFormatter(m.getType())));
    }

    fmt::print(os, FMT_STRING("(record {})"),
               fmt::join(fields.begin(), fields.end(), " "));
  }
  void visit(const types::RefType *v) override {
    fmt::print(os, FMT_STRING("(ref {})"), makeFormatter(v->getContents()));
  }
  void visit(const types::FuncType *v) override {
    auto args = makeFormatters(v->begin(), v->end());
    fmt::print(os, FMT_STRING("(func {} {})"), fmt::join(args.begin(), args.end(), " "),
               makeFormatter(v->getReturnType()));
  }
  void visit(const types::OptionalType *v) override {
    fmt::print(os, FMT_STRING("(optional {})"), makeFormatter(v->getBase()));
  }
  void visit(const types::PointerType *v) override {
    fmt::print(os, FMT_STRING("(pointer {})"), makeFormatter(v->getBase()));
  }
  void visit(const types::GeneratorType *v) override {
    fmt::print(os, FMT_STRING("(generator {})"), makeFormatter(v->getBase()));
  }
  void visit(const types::IntNType *v) override {
    fmt::print(os, FMT_STRING("(intn {} (signed {}))"), v->getLen(), v->isSigned());
  }
  void visit(const dsl::types::CustomType *v) override { v->doFormat(os); }

  void format(const Node *n) {
    if (n)
      n->accept(*this);
    else
      os << "(null)";
  }

  void format(const types::Type *t, bool canShowFull = false) {
    if (t) {
      if (seenTypes.find(t->getName()) != seenTypes.end() || !canShowFull)
        fmt::print(os, FMT_STRING("(type '\"{}\")"), t->referenceString());
      else {
        seenTypes.insert(t->getName());
        t->accept(*this);
      }
    } else
      os << "(null)";
  }

  void format(const Value *t) {
    if (t) {
      if (seenNodes.find(t->getId()) != seenNodes.end())
        fmt::print(os, FMT_STRING("(value '\"{}\")"), t->referenceString());
      else {
        seenNodes.insert(t->getId());
        t->accept(*this);
      }

    } else
      os << "(null)";
  }

  void format(const Var *t, bool canShowFull = false) {
    if (t) {
      if (seenNodes.find(t->getId()) != seenNodes.end() || !canShowFull)
        fmt::print(os, FMT_STRING("(var '\"{}\")"), t->referenceString());
      else {
        seenNodes.insert(t->getId());
        t->accept(*this);
      }
    } else
      os << "(null)";
  }

private:
  NodeFormatter makeFormatter(const types::Type *node, bool canShowFull = false) {
    auto ret = NodeFormatter(node, seenNodes, seenTypes);
    ret.canShowFull = canShowFull;
    return ret;
  }
  NodeFormatter makeFormatter(const Value *node) {
    return NodeFormatter(node, seenNodes, seenTypes);
  }
  NodeFormatter makeFormatter(const Var *node, bool canShowFull = false) {
    auto ret = NodeFormatter(node, seenNodes, seenTypes);
    ret.canShowFull = canShowFull;
    return ret;
  }

  template <typename It> std::vector<NodeFormatter> makeFormatters(It begin, It end) {
    std::vector<NodeFormatter> ret;
    while (begin != end) {
      ret.push_back(makeFormatter(*begin));
      ++begin;
    }
    return ret;
  }
  template <typename It>
  std::vector<NodeFormatter> makeFormatters(It begin, It end, bool canShowFull) {
    std::vector<NodeFormatter> ret;
    while (begin != end) {
      ret.push_back(makeFormatter(*begin, canShowFull));
      ++begin;
    }
    return ret;
  }
};

std::ostream &operator<<(std::ostream &os, const NodeFormatter &n) {
  FormatVisitor fv(os, n.seenNodes, n.seenTypes);
  if (n.type)
    fv.format(n.type, n.canShowFull);
  else if (n.value)
    fv.format(n.value);
  else
    fv.format(n.var, n.canShowFull);
  return os;
}
} // namespace

// See https://github.com/fmtlib/fmt/issues/1283.
namespace fmt {
template <typename Char>
struct formatter<NodeFormatter, Char>
    : fmt::v6::internal::fallback_formatter<NodeFormatter, Char> {};
} // namespace fmt

namespace seq {
namespace ir {
namespace util {

std::string format(const Node *node) {
  std::stringstream ss;
  format(ss, node);
  return ss.str();
}

std::ostream &format(std::ostream &os, const Node *node) {
  std::unordered_set<int> seenNodes;
  std::unordered_set<std::string> seenTypes;

  FormatVisitor fv(os, seenNodes, seenTypes);
  fv.format(node);

  return os;
}

} // namespace util
} // namespace ir
} // namespace seq
