#include "seq.h"
#include "exc.h"
#include "copy.h"

using namespace seq;
using namespace llvm;

Copy::Copy() :
    Stage("copy", types::SeqType::get(), types::SeqType::get()), copyFunc(nullptr)
{
}

void Copy::codegen(Module *module)
{
	ensurePrev();
	validate();

	LLVMContext& context = module->getContext();
	Value *seq = getSafe(prev->outs, SeqData::SEQ);
	Value *len = getSafe(prev->outs, SeqData::LEN);

	if (!copyFunc) {
		copyFunc = cast<Function>(
		             module->getOrInsertFunction(
		               "copy",
		               IntegerType::getInt8PtrTy(context),
		               IntegerType::getInt8PtrTy(context),
		               seqIntLLVM(context)));
	}

	block = prev->block;
	IRBuilder<> builder(block);

	std::vector<Value *> args = {seq, len};
	Value *copy = builder.CreateCall(copyFunc, args);

	outs->insert({SeqData::SEQ, copy});
	outs->insert({SeqData::LEN, len});

	codegenNext(module);
	prev->setAfter(getAfter());
}

void Copy::finalize(ExecutionEngine *eng)
{
	eng->addGlobalMapping(copyFunc, (void *)util::copy);
}

Copy& Copy::make()
{
	return *new Copy();
}
