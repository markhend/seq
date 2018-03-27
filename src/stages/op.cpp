#include <string>
#include <vector>
#include "exc.h"
#include "op.h"

using namespace seq;
using namespace llvm;

Op::Op(std::string name, SeqOp op) :
    Stage(std::move(name), types::SeqType::get(), types::SeqType::get()), op(op)
{
}

void Op::codegen(Module *module)
{
	ensurePrev();
	validate();

	LLVMContext& context = module->getContext();
	func = cast<Function>(
	         module->getOrInsertFunction(
               name,
	           Type::getVoidTy(context),
	           IntegerType::getInt8PtrTy(context),
	           seqIntLLVM(context)));

	func->setCallingConv(CallingConv::C);

	block = prev->block;
	outs->insert(prev->outs->begin(), prev->outs->end());
	IRBuilder<> builder(block);
	std::vector<Value *> args = {getSafe(outs, SeqData::SEQ), getSafe(outs, SeqData::LEN)};
	builder.CreateCall(func, args, "");

	codegenNext(module);
	prev->setAfter(getAfter());
}

void Op::finalize(ExecutionEngine *eng)
{
	eng->addGlobalMapping(func, (void *)op);
	Stage::finalize(eng);
}

Op& Op::make(std::string name, SeqOp op)
{
	return *new Op(std::move(name), op);
}
