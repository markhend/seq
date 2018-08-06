#include "seq/seq.h"
#include "seq/exprstage.h"

using namespace seq;
using namespace llvm;

Print::Print(Expr *expr) :
    Stage("print"), expr(expr)
{
}

void Print::codegen(BasicBlock*& block)
{
	types::Type *type = expr->getType();
	Value *val = expr->codegen(getBase(), block);
	type->print(getBase(), val, block);
}

Print& Print::make(Expr *expr)
{
	return *new Print(expr);
}

Print *Print::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (Print *)ref->getClone(this);

	Print& x = Print::make(expr->clone(ref));
	ref->addClone(this, &x);
	Stage::setCloneBase(&x, ref);
	return &x;
}

ExprStage::ExprStage(Expr *expr) :
    Stage("expr"), expr(expr)
{
}

void ExprStage::codegen(BasicBlock*& block)
{
	expr->codegen(getBase(), block);
}

ExprStage& ExprStage::make(Expr *expr)
{
	return *new ExprStage(expr);
}

ExprStage *ExprStage::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (ExprStage *)ref->getClone(this);

	ExprStage& x = ExprStage::make(expr->clone(ref));
	ref->addClone(this, &x);
	Stage::setCloneBase(&x, ref);
	return &x;
}

CellStage::CellStage(Expr *init) :
    Stage("cell"), init(init), var(new Cell())
{
}

Cell *CellStage::getVar()
{
	return var;
}

void CellStage::codegen(BasicBlock*& block)
{
	var->setType(init->getType());
	Value *val = init->codegen(getBase(), block);
	var->store(getBase(), val, block);
}

CellStage& CellStage::make(Expr *init)
{
	return *new CellStage(init);
}

CellStage *CellStage::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (CellStage *)ref->getClone(this);

	CellStage& x = CellStage::make(init->clone(ref));
	ref->addClone(this, &x);
	x.var = var->clone(ref);
	Stage::setCloneBase(&x, ref);
	return &x;
}

AssignStage::AssignStage(Cell *cell, Expr *value) :
    Stage("(=)"), cell(cell), value(value)
{
}

void AssignStage::codegen(BasicBlock*& block)
{
	value->ensure(cell->getType());
	Value *val = value->codegen(getBase(), block);
	cell->store(getBase(), val, block);
}

AssignStage& AssignStage::make(Cell *cell, Expr *value)
{
	return *new AssignStage(cell, value);
}

AssignStage *AssignStage::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (AssignStage *)ref->getClone(this);

	AssignStage& x = AssignStage::make(cell->clone(ref), value->clone(ref));
	ref->addClone(this, &x);
	Stage::setCloneBase(&x, ref);
	return &x;
}

AssignIndexStage::AssignIndexStage(Expr *array, Expr *idx, Expr *value) :
    Stage("([]=)"), array(array), idx(idx), value(value)
{
}

void AssignIndexStage::codegen(BasicBlock*& block)
{
	this->idx->ensure(types::IntType::get());

	if (!array->getType()->isGeneric(types::ArrayType::get()))
		throw exc::SeqException("can only assign indices of array type");

	auto *arrType = dynamic_cast<types::ArrayType *>(array->getType());
	assert(arrType != nullptr);
	value->ensure(arrType->indexType());

	Value *val = value->codegen(getBase(), block);
	Value *arr = array->codegen(getBase(), block);
	Value *idx = this->idx->codegen(getBase(), block);
	array->getType()->indexStore(getBase(), arr, idx, val, block);
}

AssignIndexStage& AssignIndexStage::make(Expr *array, Expr *idx, Expr *value)
{
	return *new AssignIndexStage(array, idx, value);
}

AssignIndexStage *AssignIndexStage::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (AssignIndexStage *)ref->getClone(this);

	AssignIndexStage& x = AssignIndexStage::make(array->clone(ref), idx->clone(ref), value->clone(ref));
	ref->addClone(this, &x);
	Stage::setCloneBase(&x, ref);
	return &x;
}

AssignMemberStage::AssignMemberStage(Expr *expr, std::string memb, Expr *value) :
    Stage("(.=)"), expr(expr), memb(std::move(memb)), value(value)
{
}

AssignMemberStage::AssignMemberStage(Expr *expr, seq_int_t idx, Expr *value) :
    AssignMemberStage(expr, std::to_string(idx), value)
{
}

void AssignMemberStage::codegen(BasicBlock*& block)
{
	value->ensure(expr->getType()->membType(memb));
	Value *x = expr->codegen(getBase(), block);
	Value *v = value->codegen(getBase(), block);
	expr->getType()->setMemb(x, memb, v, block);
}

AssignMemberStage& AssignMemberStage::make(Expr *expr, std::string memb, Expr *value)
{
	return *new AssignMemberStage(expr, std::move(memb), value);
}

AssignMemberStage& AssignMemberStage::make(Expr *expr, seq_int_t idx, Expr *value)
{
	return *new AssignMemberStage(expr, idx, value);
}

AssignMemberStage *AssignMemberStage::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (AssignMemberStage *)ref->getClone(this);

	AssignMemberStage& x = AssignMemberStage::make(expr->clone(ref), memb, value->clone(ref));
	ref->addClone(this, &x);
	Stage::setCloneBase(&x, ref);
	return &x;
}

If::If() :
    Stage("if"), conds(), branches(), elseAdded(false)
{
}

void If::codegen(BasicBlock*& block)
{
	if (conds.empty())
		throw exc::SeqException("no conditions added to if-stage");

	assert(conds.size() == branches.size());

	for (auto *cond : conds)
		cond->ensure(types::BoolType::get());

	LLVMContext& context = block->getContext();
	Function *func = block->getParent();
	IRBuilder<> builder(block);

	std::vector<BranchInst *> binsts;

	for (unsigned i = 0; i < conds.size(); i++) {
		Value *cond = conds[i]->codegen(getBase(), block);
		Block *branch = branches[i];

		builder.SetInsertPoint(block);  // recall: expr codegen can change the block
		cond = builder.CreateTrunc(cond, IntegerType::getInt1Ty(context));

		BasicBlock *b1 = BasicBlock::Create(context, "", func);
		BranchInst *binst1 = builder.CreateCondBr(cond, b1, b1);  // we set false-branch below

		block = b1;
		branch->codegen(block);
		builder.SetInsertPoint(block);
		BranchInst *binst2 = builder.CreateBr(b1);  // we reset this below
		binsts.push_back(binst2);

		BasicBlock *b2 = BasicBlock::Create(context, "", func);
		binst1->setSuccessor(1, b2);
		block = b2;
	}

	BasicBlock *after = BasicBlock::Create(context, "", func);
	builder.SetInsertPoint(block);
	builder.CreateBr(after);

	for (auto *binst : binsts)
		binst->setSuccessor(0, after);

	block = after;
}

If& If::make()
{
	return *new If();
}

Block *If::addCond(Expr *cond)
{
	if (elseAdded)
		throw exc::SeqException("cannot add else-if branch to if-stage after else branch");

	auto *branch = new Block(this);
	conds.push_back(cond);
	branches.push_back(branch);
	return branch;
}

Block *If::addElse()
{
	if (elseAdded)
		throw exc::SeqException("cannot add second else branch to if-stage");

	Block *branch = addCond(new BoolExpr(true));
	elseAdded = true;
	return branch;
}

If *If::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (If *)ref->getClone(this);

	If& x = If::make();
	ref->addClone(this, &x);

	std::vector<Expr *> condsCloned;
	std::vector<Block *> branchesCloned;

	for (auto *cond : conds)
		condsCloned.push_back(cond->clone(ref));

	for (auto *branch : branches)
		branchesCloned.push_back(branch->clone(ref));

	x.conds = condsCloned;
	x.branches = branchesCloned;
	x.elseAdded = elseAdded;

	Stage::setCloneBase(&x, ref);
	return &x;
}

Match::Match() :
    Stage("match"), value(nullptr), patterns(), branches()
{
}

void Match::codegen(BasicBlock*& block)
{
	if (patterns.empty())
		throw exc::SeqException("no patterns added to match-stage");

	assert(patterns.size() == branches.size() && value);

	LLVMContext& context = block->getContext();
	Function *func = block->getParent();

	IRBuilder<> builder(block);
	types::Type *valType = value->getType();

	bool seenCatchAll = false;
	for (auto *pattern : patterns) {
		if (pattern->isCatchAll())
			seenCatchAll = true;
	}

	if (!seenCatchAll)
		throw exc::SeqException("match statement missing catch-all pattern");

	Value *val = value->codegen(getBase(), block);

	std::vector<BranchInst *> binsts;

	for (unsigned i = 0; i < patterns.size(); i++) {
		Value *cond = patterns[i]->codegen(getBase(), valType, val, block);
		Block *branch = branches[i];

		builder.SetInsertPoint(block);  // recall: expr codegen can change the block

		BasicBlock *b1 = BasicBlock::Create(context, "", func);
		BranchInst *binst1 = builder.CreateCondBr(cond, b1, b1);  // we set false-branch below

		block = b1;
		branch->codegen(block);
		builder.SetInsertPoint(block);
		BranchInst *binst2 = builder.CreateBr(b1);  // we reset this below
		binsts.push_back(binst2);

		BasicBlock *b2 = BasicBlock::Create(context, "", func);
		binst1->setSuccessor(1, b2);
		block = b2;
	}

	BasicBlock *after = BasicBlock::Create(context, "", func);
	builder.SetInsertPoint(block);
	builder.CreateBr(after);

	for (auto *binst : binsts)
		binst->setSuccessor(0, after);

	block = after;
}

Match& Match::make()
{
	return *new Match();
}

void Match::setValue(Expr *value)
{
	if (this->value)
		throw exc::SeqException("cannot re-set match stage's value");

	this->value = value;
}

Block *Match::addCase(Pattern *pattern)
{
	auto *branch = new Block(this);
	patterns.push_back(pattern);
	branches.push_back(branch);
	return branch;
}

Match *Match::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (Match *)ref->getClone(this);

	Match& x = Match::make();
	ref->addClone(this, &x);

	std::vector<Pattern *> patternsCloned;
	std::vector<Block *> branchesCloned;

	for (auto *pattern : patterns)
		patternsCloned.push_back(pattern->clone(ref));

	for (auto *branch : branches)
		branchesCloned.push_back(branch->clone(ref));

	if (value) x.value = value->clone(ref);
	x.patterns = patternsCloned;
	x.branches = branchesCloned;

	Stage::setCloneBase(&x, ref);
	return &x;
}

While::While(Expr *cond) :
    Stage("while"), cond(cond), scope(new Block(this))
{
	loop = true;
}

Block *While::getBlock()
{
	return scope;
}

void While::codegen(BasicBlock*& block)
{
	cond->ensure(types::BoolType::get());
	LLVMContext& context = block->getContext();

	BasicBlock *entry = block;
	Function *func = entry->getParent();

	IRBuilder<> builder(entry);

	BasicBlock *loop0 = BasicBlock::Create(context, "while", func);
	BasicBlock *loop = loop0;
	builder.CreateBr(loop);

	Value *cond = this->cond->codegen(getBase(), loop);  // recall: this can change `loop`
	builder.SetInsertPoint(loop);
	cond = builder.CreateTrunc(cond, IntegerType::getInt1Ty(context));

	BasicBlock *body = BasicBlock::Create(context, "body", func);
	BranchInst *branch = builder.CreateCondBr(cond, body, body);  // we set false-branch below

	block = body;

	scope->codegen(block);

	builder.SetInsertPoint(block);
	builder.CreateBr(loop0);

	BasicBlock *exit = BasicBlock::Create(context, "exit", func);
	branch->setSuccessor(1, exit);
	block = exit;

	setBreaks(exit);
	setContinues(loop0);
}

While& While::make(Expr *cond)
{
	return *new While(cond);
}

While *While::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (While *)ref->getClone(this);

	While& x = While::make(cond->clone(ref));
	ref->addClone(this, &x);
	delete x.scope;
	x.scope = scope->clone(ref);
	Stage::setCloneBase(&x, ref);
	return &x;
}

For::For(Expr *gen) :
    Stage("for"), gen(gen), scope(new Block(this)), var(new Cell())
{
	loop = true;
}

Block *For::getBlock()
{
	return scope;
}

Cell *For::getVar()
{
	return var;
}

void For::codegen(BasicBlock*& block)
{
	if (!gen->getType()->isGeneric(types::GenType::get()))
		throw exc::SeqException("cannot iterate over non-generator");

	auto *type = dynamic_cast<types::GenType *>(gen->getType());
	assert(type);

	LLVMContext& context = block->getContext();

	BasicBlock *entry = block;
	Function *func = entry->getParent();

	var->setType(type->getBaseType(0));
	Value *gen = this->gen->codegen(getBase(), entry);
	IRBuilder<> builder(entry);

	BasicBlock *loopCont = BasicBlock::Create(context, "for_cont", func);
	BasicBlock *loop = BasicBlock::Create(context, "for", func);
	builder.CreateBr(loop);

	builder.SetInsertPoint(loopCont);
	builder.CreateBr(loop);

	builder.SetInsertPoint(loop);
	type->resume(gen, loop);
	Value *cond = type->done(gen, loop);
	BasicBlock *body = BasicBlock::Create(context, "body", func);
	BranchInst *branch = builder.CreateCondBr(cond, body, body);  // we set true-branch below

	block = body;
	Value *val = type->promise(gen, block);
	var->store(getBase(), val, block);

	scope->codegen(block);

	builder.SetInsertPoint(block);
	builder.CreateBr(loop);

	BasicBlock *cleanup = BasicBlock::Create(context, "cleanup", func);
	branch->setSuccessor(0, cleanup);
	type->destroy(gen, cleanup);

	builder.SetInsertPoint(cleanup);
	BasicBlock *exit = BasicBlock::Create(context, "exit", func);
	builder.CreateBr(exit);
	block = exit;

	setBreaks(exit);
	setContinues(loopCont);
}

For& For::make(Expr *gen)
{
	return *new For(gen);
}

For *For::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (For *)ref->getClone(this);

	For& x = For::make(gen->clone(ref));
	ref->addClone(this, &x);
	delete x.scope;
	x.scope = scope->clone(ref);
	x.var = var->clone(ref);
	Stage::setCloneBase(&x, ref);
	return &x;
}

Return::Return(Expr *expr) :
    Stage("return"), expr(expr)
{
}

void Return::codegen(BasicBlock*& block)
{
	types::Type *type = expr ? expr->getType() : types::VoidType::get();
	Value *val = expr ? expr->codegen(getBase(), block) : nullptr;
	getBase()->codegenReturn(val, type, block);
}

Return& Return::make(Expr *expr)
{
	return *new Return(expr);
}

Return *Return::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (Return *)ref->getClone(this);

	Return& x = Return::make(expr ? expr->clone(ref) : nullptr);
	ref->addClone(this, &x);
	Stage::setCloneBase(&x, ref);
	return &x;
}

Yield::Yield(Expr *expr) :
    Stage("Yield"), expr(expr)
{
}

void Yield::codegen(BasicBlock*& block)
{
	types::Type *type = expr ? expr->getType() : types::VoidType::get();
	Value *val = expr ? expr->codegen(getBase(), block) : nullptr;
	getBase()->codegenYield(val, type, block);
}

Yield& Yield::make(Expr *expr)
{
	return *new Yield(expr);
}

Yield *Yield::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (Yield *)ref->getClone(this);

	Yield& x = Yield::make(expr ? expr->clone(ref) : nullptr);
	ref->addClone(this, &x);
	Stage::setCloneBase(&x, ref);
	return &x;
}

Break::Break() :
    Stage("break")
{
}

void Break::codegen(BasicBlock*& block)
{
	LLVMContext& context = block->getContext();
	IRBuilder<> builder(block);
	BranchInst *inst = builder.CreateBr(block);  // destination will be fixed by `setBreaks`
	addBreakToEnclosingLoop(inst);
	block = BasicBlock::Create(context, "", block->getParent());
}

Break& Break::make()
{
	return *new Break();
}

Break *Break::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (Break *)ref->getClone(this);

	Break& x = Break::make();
	ref->addClone(this, &x);
	Stage::setCloneBase(&x, ref);
	return &x;
}

Continue::Continue() :
    Stage("continue")
{
}

void Continue::codegen(BasicBlock*& block)
{
	LLVMContext& context = block->getContext();
	IRBuilder<> builder(block);
	BranchInst *inst = builder.CreateBr(block);  // destination will be fixed by `setContinues`
	addContinueToEnclosingLoop(inst);
	block = BasicBlock::Create(context, "", block->getParent());
}

Continue& Continue::make()
{
	return *new Continue();
}

Continue *Continue::clone(types::RefType *ref)
{
	if (ref->seenClone(this))
		return (Continue *)ref->getClone(this);

	Continue& x = Continue::make();
	ref->addClone(this, &x);
	Stage::setCloneBase(&x, ref);
	return &x;
}
