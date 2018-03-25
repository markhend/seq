#include <cstdlib>
#include <iostream>
#include <vector>
#include "seq.h"

using namespace seq;
using namespace llvm;

types::Type::Type(std::string name, types::Type *parent, SeqData key) :
    name(std::move(name)), parent(parent), key(key)
{
}

types::Type::Type(std::string name, Type *parent) :
    Type(std::move(name), parent, SeqData::NONE)
{
}

void types::Type::callPrint(seq::Seq *base,
                            ValMap outs,
                            BasicBlock *block)
{
	if (!vtable.print || getKey() == SeqData::NONE)
		throw exc::SeqException("cannot print type '" + getName() + "'");

	if (!vtable.printFunc) {
		vtable.printFunc = cast<Function>(
		                     block->getModule()->getOrInsertFunction(
		                       "print" + getName(),
		                       llvm::Type::getVoidTy(block->getContext()),
		                       getLLVMType(block->getContext())));

		vtable.printFunc->setCallingConv(CallingConv::C);
	}

	auto iter = outs->find(getKey());

	if (iter == outs->end())
		throw exc::SeqException("pipeline error");

	IRBuilder<> builder(block);
	std::vector<Value *> args = {iter->second};
	builder.CreateCall(vtable.printFunc, args, "");
}

void types::Type::finalizePrint(ExecutionEngine *eng)
{
	eng->addGlobalMapping(vtable.printFunc, vtable.print);
}

void types::Type::callSerialize(seq::Seq *base,
                                ValMap outs,
                                BasicBlock *block,
                                std::string file)
{
	if (!vtable.serialize || getKey() == SeqData::NONE)
		throw exc::SeqException("type '" + getName() + "' cannot be serialized");

	LLVMContext& context = block->getContext();
	Module *module = block->getModule();

	if (!vtable.serializeFunc) {
		vtable.serializeFunc = cast<Function>(
		                         block->getModule()->getOrInsertFunction(
		                           "serialize" + getName(),
		                           llvm::Type::getVoidTy(context),
		                           getLLVMType(context),
		                           IntegerType::getInt8PtrTy(context)));

		vtable.serializeFunc->setCallingConv(CallingConv::C);
	}

	GlobalVariable *fileVar = new GlobalVariable(*module,
	                                             llvm::ArrayType::get(IntegerType::getInt8Ty(context),
	                                                                  file.length() + 1),
	                                             true,
	                                             GlobalValue::PrivateLinkage,
	                                             ConstantDataArray::getString(context, file),
	                                             "file");
	fileVar->setAlignment(1);

	auto iter = outs->find(getKey());

	if (iter == outs->end())
		throw exc::SeqException("pipeline error");

	IRBuilder<> builder(block);
	Value *filename = builder.CreateGEP(fileVar, ConstantInt::get(seqIntLLVM(context), 0, false));
	std::vector<Value *> args = {iter->second, filename};
	builder.CreateCall(vtable.serializeFunc, args, "");
}

void types::Type::finalizeSerialize(ExecutionEngine *eng)
{
	eng->addGlobalMapping(vtable.serializeFunc, vtable.serialize);
}

void types::Type::callDeserialize(seq::Seq *base,
                                  ValMap outs,
                                  BasicBlock *block,
                                  std::string file)
{
	if (!vtable.deserialize || getKey() == SeqData::NONE)
		throw exc::SeqException("type '" + getName() + "' cannot be serialized");

	LLVMContext& context = block->getContext();
	Module *module = block->getModule();

	if (!vtable.deserializeFunc) {
		vtable.deserializeFunc = cast<Function>(
		                           block->getModule()->getOrInsertFunction(
		                             "deserialize" + getName(),
		                             getLLVMType(context),
		                             IntegerType::getInt8PtrTy(context)));

		vtable.deserializeFunc->setCallingConv(CallingConv::C);
	}

	GlobalVariable *fileVar = new GlobalVariable(*module,
	                                             llvm::ArrayType::get(IntegerType::getInt8Ty(context), file.length() + 1),
	                                             true,
	                                             GlobalValue::PrivateLinkage,
	                                             ConstantDataArray::getString(context, file),
	                                             "file");
	fileVar->setAlignment(1);

	IRBuilder<> builder(block);
	Value *filename = builder.CreateGEP(fileVar, ConstantInt::get(seqIntLLVM(context), 0, false));
	std::vector<Value *> args = {filename};
	Value *result = builder.CreateCall(vtable.deserializeFunc, args, "");
	outs->insert({getKey(), result});
}

void types::Type::finalizeDeserialize(ExecutionEngine *eng)
{
	eng->addGlobalMapping(vtable.deserializeFunc, vtable.deserialize);
}

void types::Type::callSerializeArray(seq::Seq *base,
                                     ValMap outs,
                                     BasicBlock *block,
                                     std::string file)
{
	if (!vtable.serializeArray)
		throw exc::SeqException("array of type '" + getName() + "' cannot be serialized");

	LLVMContext& context = block->getContext();
	Module *module = block->getModule();

	if (!vtable.serializeArrayFunc) {
		vtable.serializeArrayFunc = cast<Function>(
		                              block->getModule()->getOrInsertFunction(
		                                "serialize" + getName() + "Array",
		                                llvm::Type::getVoidTy(context),
		                                PointerType::get(getLLVMArrayType(context), 0),
		                                seqIntLLVM(context),
		                                IntegerType::getInt8PtrTy(context)));

		vtable.serializeArrayFunc->setCallingConv(CallingConv::C);
	}

	GlobalVariable *fileVar = new GlobalVariable(*module,
	                                             llvm::ArrayType::get(IntegerType::getInt8Ty(context), file.length() + 1),
	                                             true,
	                                             GlobalValue::PrivateLinkage,
	                                             ConstantDataArray::getString(context, file),
	                                             "file");
	fileVar->setAlignment(1);

	auto ptriter = outs->find(SeqData::ARRAY);
	auto leniter = outs->find(SeqData::LEN);

	if (ptriter == outs->end() || leniter == outs->end())
		throw exc::SeqException("pipeline error");

	Value *ptr = ptriter->second;
	Value *len = leniter->second;

	IRBuilder<> builder(block);
	ptr = builder.CreateLoad(ptr);
	Value *filename = builder.CreateGEP(fileVar, ConstantInt::get(seqIntLLVM(context), 0, false));
	std::vector<Value *> args = {ptr, len, filename};
	builder.CreateCall(vtable.serializeArrayFunc, args, "");
}

void types::Type::finalizeSerializeArray(ExecutionEngine *eng)
{
	eng->addGlobalMapping(vtable.serializeArrayFunc, vtable.serializeArray);
}

void types::Type::callDeserializeArray(seq::Seq *base,
                                       ValMap outs,
                                       BasicBlock *block,
                                       std::string file)
{
	if (!vtable.deserializeArray)
		throw exc::SeqException("array of type '" + getName() + "' cannot be deserialized");

	LLVMContext& context = block->getContext();
	Module *module = block->getModule();

	if (!vtable.deserializeArrayFunc) {
		vtable.deserializeArrayFunc = cast<Function>(
		                                block->getModule()->getOrInsertFunction(
		                                  "deserialize" + getName() + "Array",
		                                  PointerType::get(getLLVMArrayType(context), 0),
		                                  IntegerType::getInt8PtrTy(context),
		                                  PointerType::get(seqIntLLVM(context), 0)));

		vtable.deserializeArrayFunc->setCallingConv(CallingConv::C);
	}

	GlobalVariable *fileVar = new GlobalVariable(*module,
	                                             llvm::ArrayType::get(IntegerType::getInt8Ty(context), file.length() + 1),
	                                             true,
	                                             GlobalValue::PrivateLinkage,
	                                             ConstantDataArray::getString(context, file),
	                                             "file");
	fileVar->setAlignment(1);

	IRBuilder<> preamble(base->getPreamble());
	IRBuilder<> builder(block);
	Value *filename = builder.CreateGEP(fileVar, ConstantInt::get(seqIntLLVM(context), 0));
	Value *len = preamble.CreateAlloca(seqIntLLVM(context), ConstantInt::get(seqIntLLVM(context), 1));
	std::vector<Value *> args = {filename, len};
	Value *mem = builder.CreateCall(vtable.deserializeArrayFunc, args, "");

	GlobalVariable *ptr = new GlobalVariable(*module,
	                                         PointerType::get(getLLVMArrayType(context), 0),
	                                         false,
	                                         GlobalValue::PrivateLinkage,
	                                         nullptr,
	                                         "mem");

	ptr->setInitializer(
	  ConstantPointerNull::get(PointerType::get(getLLVMArrayType(context), 0)));
	builder.CreateStore(mem, ptr);

	outs->insert({SeqData::ARRAY, ptr});
	outs->insert({SeqData::LEN, builder.CreateLoad(len)});
}

void types::Type::finalizeDeserializeArray(ExecutionEngine *eng)
{
	eng->addGlobalMapping(vtable.deserializeArrayFunc, vtable.deserializeArray);
}

void types::Type::callAlloc(seq::Seq *base,
                            ValMap outs,
                            seq_int_t count,
                            BasicBlock *block)
{
	if (size() == 0)
		throw exc::SeqException("cannot create array of type '" + getName() + "'");

	LLVMContext& context = block->getContext();
	Module *module = block->getModule();

	if (!vtable.allocFunc) {
		vtable.allocFunc = cast<Function>(
		                     module->getOrInsertFunction(
		                       "malloc",
		                       IntegerType::getInt8PtrTy(context),
		                       IntegerType::getIntNTy(context, sizeof(size_t)*8)));
	}

	IRBuilder<> builder(block);

	GlobalVariable *ptr = new GlobalVariable(*module,
	                                         PointerType::get(getLLVMArrayType(context), 0),
	                                         false,
	                                         GlobalValue::PrivateLinkage,
	                                         nullptr,
	                                         "mem");

	ptr->setInitializer(
	  ConstantPointerNull::get(PointerType::get(getLLVMArrayType(context), 0)));

	std::vector<Value *> args = {
	  ConstantInt::get(IntegerType::getIntNTy(context, sizeof(size_t)*8), (unsigned)(count*arraySize()))};
	Value *mem = builder.CreateCall(vtable.allocFunc, args);
	mem = builder.CreatePointerCast(mem, PointerType::get(getLLVMArrayType(context), 0));
	builder.CreateStore(mem, ptr);

	outs->insert({SeqData::ARRAY, ptr});
	outs->insert({SeqData::LEN, ConstantInt::get(seqIntLLVM(context), (uint64_t)count)});
}

void types::Type::finalizeAlloc(ExecutionEngine *eng)
{
	eng->addGlobalMapping(vtable.allocFunc, (void *)std::malloc);
}

void types::Type::codegenLoad(seq::Seq *base,
                              ValMap outs,
                              BasicBlock *block,
                              Value *ptr,
                              Value *idx)
{
	if (size() == 0 || getKey() == SeqData::NONE)
		throw exc::SeqException("cannot load type '" + getName() + "'");

	IRBuilder<> builder(block);
	Value *val = builder.CreateLoad(builder.CreateGEP(ptr, idx));
	outs->insert({getKey(), val});
}

void types::Type::codegenStore(seq::Seq *base,
                               ValMap outs,
                               BasicBlock *block,
                               Value *ptr,
                               Value *idx)
{
	if (size() == 0|| getKey() == SeqData::NONE)
		throw exc::SeqException("cannot store type '" + getName() + "'");

	auto valiter = outs->find(getKey());

	if (valiter == outs->end())
		throw exc::SeqException("pipeline error");

	Value *val = valiter->second;

	IRBuilder<> builder(block);
	builder.CreateStore(val, builder.CreateGEP(ptr, idx));
}

bool types::Type::isChildOf(types::Type *type)
{
	return (getName() == type->getName()) || (parent && parent->isChildOf(type));
}

std::string types::Type::getName() const
{
	return name;
}

SeqData types::Type::getKey() const
{
	return key;
}

Type *types::Type::getLLVMType(LLVMContext& context)
{
	throw exc::SeqException("cannot instantiate '" + getName() + "' class");
}

Type *types::Type::getLLVMArrayType(LLVMContext& context)
{
	return getLLVMType(context);
}

seq_int_t types::Type::size() const
{
	return 0;
}

seq_int_t types::Type::arraySize() const
{
	return size();
}

Mem& types::Type::operator[](seq_int_t size)
{
	return Mem::make(this, size);
}