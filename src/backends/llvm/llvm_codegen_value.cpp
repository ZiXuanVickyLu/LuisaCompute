//
// Created by Mike Smith on 2022/5/23.
//

#include <backends/llvm/llvm_codegen.h>

namespace luisa::compute::llvm {

::llvm::Value *LLVMCodegen::_literal(int x) noexcept {
    auto b = _current_context()->builder.get();
    return b->getInt32(x);
}

::llvm::Value *LLVMCodegen::_literal(uint x) noexcept {
    auto b = _current_context()->builder.get();
    return b->getInt32(x);
}

::llvm::Value *LLVMCodegen::_literal(bool x) noexcept {
    auto b = _current_context()->builder.get();
    return b->getInt1(x);
}

::llvm::Value *LLVMCodegen::_literal(float x) noexcept {
    auto b = _current_context()->builder.get();
    return ::llvm::ConstantFP::get(b->getFloatTy(), x);
}

::llvm::Value *LLVMCodegen::_literal(int2 x) noexcept {
    return _literal(make_uint2(x));
}

::llvm::Value *LLVMCodegen::_literal(uint2 x) noexcept {
    return ::llvm::ConstantDataVector::get(
        _context, std::array{x.x, x.y});
}

::llvm::Value *LLVMCodegen::_literal(bool2 x) noexcept {
    return ::llvm::ConstantDataVector::get(
        _context, std::array{static_cast<uint8_t>(x.x),
                             static_cast<uint8_t>(x.y)});
}

::llvm::Value *LLVMCodegen::_literal(float2 x) noexcept {
    return ::llvm::ConstantDataVector::get(
        _context, std::array{x.x, x.y});
}

::llvm::Value *LLVMCodegen::_literal(int3 x) noexcept {
    return _literal(make_uint3(x));
}

::llvm::Value *LLVMCodegen::_literal(uint3 x) noexcept {
    return _literal(make_uint4(x, 0u));
}

::llvm::Value *LLVMCodegen::_literal(bool3 x) noexcept {
    return _literal(make_bool4(x, false));
}

::llvm::Value *LLVMCodegen::_literal(float3 x) noexcept {
    return _literal(make_float4(x, 0.0f));
}

::llvm::Value *LLVMCodegen::_literal(int4 x) noexcept {
    return _literal(make_uint4(x));
}

::llvm::Value *LLVMCodegen::_literal(uint4 x) noexcept {
    return ::llvm::ConstantDataVector::get(
        _context, std::array{x.x, x.y, x.z, x.w});
}

::llvm::Value *LLVMCodegen::_literal(bool4 x) noexcept {
    return ::llvm::ConstantDataVector::get(
        _context, std::array{static_cast<uint8_t>(x.x),
                             static_cast<uint8_t>(x.y),
                             static_cast<uint8_t>(x.z),
                             static_cast<uint8_t>(x.w)});
}

::llvm::Value *LLVMCodegen::_literal(float4 x) noexcept {
    return ::llvm::ConstantDataVector::get(
        _context, std::array{x.x, x.y, x.z, x.w});
}

::llvm::Value *LLVMCodegen::_literal(float2x2 x) noexcept {
    return ::llvm::ConstantStruct::get(
        static_cast<::llvm::StructType *>(_create_type(Type::of<float2x2>())),
        static_cast<::llvm::Constant *>(_literal(x[0])),
        static_cast<::llvm::Constant *>(_literal(x[1])));
}

::llvm::Value *LLVMCodegen::_literal(float3x3 x) noexcept {
    return ::llvm::ConstantStruct::get(
        static_cast<::llvm::StructType *>(_create_type(Type::of<float3x3>())),
        static_cast<::llvm::Constant *>(_literal(x[0])),
        static_cast<::llvm::Constant *>(_literal(x[1])),
        static_cast<::llvm::Constant *>(_literal(x[2])));
}

::llvm::Value *LLVMCodegen::_literal(float4x4 x) noexcept {
    return ::llvm::ConstantStruct::get(
        static_cast<::llvm::StructType *>(_create_type(Type::of<float4x4>())),
        static_cast<::llvm::Constant *>(_literal(x[0])),
        static_cast<::llvm::Constant *>(_literal(x[1])),
        static_cast<::llvm::Constant *>(_literal(x[2])),
        static_cast<::llvm::Constant *>(_literal(x[3])));
}

::llvm::Value *LLVMCodegen::_create_stack_variable(::llvm::Value *x, luisa::string_view name) noexcept {
    auto builder = _current_context()->builder.get();
    auto t = x->getType();
    if (t->isIntegerTy(1)) {
        // special handling for int1
        return _create_stack_variable(
            builder->CreateZExt(x, builder->getInt8Ty(), "bit_to_bool"), name);
    }
    if (t->isVectorTy() && static_cast<::llvm::VectorType *>(t)->getElementType()->isIntegerTy(1)) {
        // special handling for int1 vector
        auto dim = static_cast<::llvm::VectorType *>(t)->getElementCount();
        auto dst_type = ::llvm::VectorType::get(builder->getInt8Ty(), dim);
        return _create_stack_variable(builder->CreateZExt(x, dst_type, "bit_to_bool"), name);
    }
    auto p = builder->CreateAlloca(x->getType(), nullptr, name);
    builder->CreateStore(x, p);
    return p;
}

::llvm::Value *LLVMCodegen::_create_constant(ConstantData c) noexcept {
    auto key = c.hash();
    if (auto iter = _constants.find(key); iter != _constants.end()) {
        return iter->second;
    }
    auto value = luisa::visit(
        [this](auto s) noexcept {
            using T = std::remove_cvref_t<decltype(s[0])>;
            ::llvm::StringRef ref{reinterpret_cast<const char *>(s.data()), s.size_bytes()};
            return ::llvm::ConstantDataArray::getRaw(ref, s.size(), _create_type(Type::of<T>()));
        },
        c.view());
    auto name = luisa::format("constant_{:016x}", key);
    _module->getOrInsertGlobal(luisa::string_view{name}, value->getType());
    auto global = _module->getNamedGlobal(luisa::string_view{name});
    global->setConstant(true);
    global->setLinkage(::llvm::GlobalValue::InternalLinkage);
    global->setInitializer(value);
    global->setUnnamedAddr(::llvm::GlobalValue::UnnamedAddr::Global);
    return _constants.emplace(key, global).first->second;
}

::llvm::Value *LLVMCodegen::_make_int2(::llvm::Value *px, ::llvm::Value *py) noexcept {
    auto b = _current_context()->builder.get();
    auto x = b->CreateLoad(b->getInt32Ty(), px, "v.x");
    auto y = b->CreateLoad(b->getInt32Ty(), py, "v.y");
    auto v = static_cast<::llvm::Value *>(::llvm::UndefValue::get(
        _create_type(Type::of<int2>())));
    v = b->CreateInsertElement(v, x, 0ull, "int2.x");
    v = b->CreateInsertElement(v, y, 1ull, "int2.xy");
    return _create_stack_variable(v, "int2.addr");
}

::llvm::Value *LLVMCodegen::_make_int3(::llvm::Value *px, ::llvm::Value *py, ::llvm::Value *pz) noexcept {
    auto b = _current_context()->builder.get();
    auto x = b->CreateLoad(b->getInt32Ty(), px, "v.x");
    auto y = b->CreateLoad(b->getInt32Ty(), py, "v.y");
    auto z = b->CreateLoad(b->getInt32Ty(), pz, "v.z");
    auto v = static_cast<::llvm::Value *>(::llvm::UndefValue::get(
        _create_type(Type::of<int3>())));
    v = b->CreateInsertElement(v, x, 0ull, "int3.x");
    v = b->CreateInsertElement(v, y, 1ull, "int3.xy");
    v = b->CreateInsertElement(v, z, 2ull, "int3.xyz");
    return _create_stack_variable(v, "int3.addr");
}

::llvm::Value *LLVMCodegen::_make_int4(::llvm::Value *px, ::llvm::Value *py, ::llvm::Value *pz, ::llvm::Value *pw) noexcept {
    auto b = _current_context()->builder.get();
    auto x = b->CreateLoad(b->getInt32Ty(), px, "v.x");
    auto y = b->CreateLoad(b->getInt32Ty(), py, "v.y");
    auto z = b->CreateLoad(b->getInt32Ty(), pz, "v.z");
    auto w = b->CreateLoad(b->getInt32Ty(), pw, "v.w");
    auto v = static_cast<::llvm::Value *>(::llvm::UndefValue::get(
        _create_type(Type::of<int4>())));
    v = b->CreateInsertElement(v, x, 0ull, "int4.x");
    v = b->CreateInsertElement(v, y, 1ull, "int4.xy");
    v = b->CreateInsertElement(v, z, 2ull, "int4.xyz");
    v = b->CreateInsertElement(v, w, 3ull, "int4.xyzw");
    return _create_stack_variable(v, "int4.addr");
}

::llvm::Value *LLVMCodegen::_make_bool2(::llvm::Value *px, ::llvm::Value *py) noexcept {
    auto b = _current_context()->builder.get();
    auto x = b->CreateLoad(b->getInt8Ty(), px, "v.x");
    auto y = b->CreateLoad(b->getInt8Ty(), py, "v.y");
    auto v = static_cast<::llvm::Value *>(::llvm::UndefValue::get(
        _create_type(Type::of<bool2>())));
    v = b->CreateInsertElement(v, x, 0ull, "bool2.x");
    v = b->CreateInsertElement(v, y, 1ull, "bool2.xy");
    return _create_stack_variable(v, "bool2.addr");
}

::llvm::Value *LLVMCodegen::_make_bool3(::llvm::Value *px, ::llvm::Value *py, ::llvm::Value *pz) noexcept {
    auto b = _current_context()->builder.get();
    auto x = b->CreateLoad(b->getInt8Ty(), px, "v.x");
    auto y = b->CreateLoad(b->getInt8Ty(), py, "v.y");
    auto z = b->CreateLoad(b->getInt8Ty(), pz, "v.z");
    auto v = static_cast<::llvm::Value *>(::llvm::UndefValue::get(
        _create_type(Type::of<bool3>())));
    v = b->CreateInsertElement(v, x, 0ull, "bool3.x");
    v = b->CreateInsertElement(v, y, 1ull, "bool3.xy");
    v = b->CreateInsertElement(v, z, 2ull, "bool3.xyz");
    return _create_stack_variable(v, "bool3.addr");
}

::llvm::Value *LLVMCodegen::_make_bool4(::llvm::Value *px, ::llvm::Value *py, ::llvm::Value *pz, ::llvm::Value *pw) noexcept {
    auto b = _current_context()->builder.get();
    auto x = b->CreateLoad(b->getInt8Ty(), px, "v.x");
    auto y = b->CreateLoad(b->getInt8Ty(), py, "v.y");
    auto z = b->CreateLoad(b->getInt8Ty(), pz, "v.z");
    auto w = b->CreateLoad(b->getInt8Ty(), pw, "v.w");
    auto v = static_cast<::llvm::Value *>(::llvm::UndefValue::get(
        _create_type(Type::of<bool4>())));
    v = b->CreateInsertElement(v, x, 0ull, "bool4.x");
    v = b->CreateInsertElement(v, y, 1ull, "bool4.xy");
    v = b->CreateInsertElement(v, z, 2ull, "bool4.xyz");
    v = b->CreateInsertElement(v, w, 3ull, "bool4.xyzw");
    return _create_stack_variable(v, "bool4.addr");
}

::llvm::Value *LLVMCodegen::_make_float2(::llvm::Value *px, ::llvm::Value *py) noexcept {
    auto b = _current_context()->builder.get();
    auto x = b->CreateLoad(b->getFloatTy(), px, "v.x");
    auto y = b->CreateLoad(b->getFloatTy(), py, "v.y");
    auto v = static_cast<::llvm::Value *>(::llvm::UndefValue::get(
        _create_type(Type::of<float2>())));
    v = b->CreateInsertElement(v, x, 0ull, "float2.x");
    v = b->CreateInsertElement(v, y, 1ull, "float2.xy");
    return _create_stack_variable(v, "float2.addr");
}

::llvm::Value *LLVMCodegen::_make_float3(::llvm::Value *px, ::llvm::Value *py, ::llvm::Value *pz) noexcept {
    auto b = _current_context()->builder.get();
    auto x = b->CreateLoad(b->getFloatTy(), px, "v.x");
    auto y = b->CreateLoad(b->getFloatTy(), py, "v.y");
    auto z = b->CreateLoad(b->getFloatTy(), pz, "v.z");
    auto v = static_cast<::llvm::Value *>(::llvm::UndefValue::get(
        _create_type(Type::of<float3>())));
    v = b->CreateInsertElement(v, x, 0ull, "float3.x");
    v = b->CreateInsertElement(v, y, 1ull, "float3.xy");
    v = b->CreateInsertElement(v, z, 2ull, "float3.xyz");
    return _create_stack_variable(v, "float3.addr");
}

::llvm::Value *LLVMCodegen::_make_float4(::llvm::Value *px, ::llvm::Value *py, ::llvm::Value *pz, ::llvm::Value *pw) noexcept {
    auto b = _current_context()->builder.get();
    auto x = b->CreateLoad(b->getFloatTy(), px, "v.x");
    auto y = b->CreateLoad(b->getFloatTy(), py, "v.y");
    auto z = b->CreateLoad(b->getFloatTy(), pz, "v.z");
    auto w = b->CreateLoad(b->getFloatTy(), pw, "v.w");
    auto v = static_cast<::llvm::Value *>(::llvm::UndefValue::get(
        _create_type(Type::of<float4>())));
    v = b->CreateInsertElement(v, x, 0ull, "float4.x");
    v = b->CreateInsertElement(v, y, 1ull, "float4.xy");
    v = b->CreateInsertElement(v, z, 2ull, "float4.xyz");
    v = b->CreateInsertElement(v, w, 3ull, "float4.xyzw");
    return _create_stack_variable(v, "float4.addr");
}

::llvm::Value *LLVMCodegen::_make_float2x2(::llvm::Value *p0, ::llvm::Value *p1) noexcept {
    auto b = _current_context()->builder.get();
    auto t = _create_type(Type::of<float2x2>());
    auto m = b->CreateAlloca(t, nullptr, "float2x2.addr");
    auto m0 = b->CreateStructGEP(t, m, 0u, "float2x2.a");
    auto m1 = b->CreateStructGEP(t, m, 1u, "float2x2.b");
    b->CreateStore(b->CreateLoad(p0->getType(), p0, "m.a"), m0);
    b->CreateStore(b->CreateLoad(p1->getType(), p1, "m.b"), m1);
    return m;
}

::llvm::Value *LLVMCodegen::_make_float3x3(::llvm::Value *p0, ::llvm::Value *p1, ::llvm::Value *p2) noexcept {
    auto b = _current_context()->builder.get();
    auto t = _create_type(Type::of<float3x3>());
    auto m = b->CreateAlloca(t, nullptr, "float3x3.addr");
    auto m0 = b->CreateStructGEP(t, m, 0u, "float3x3.a");
    auto m1 = b->CreateStructGEP(t, m, 1u, "float3x3.b");
    auto m2 = b->CreateStructGEP(t, m, 2u, "float3x3.c");
    b->CreateStore(b->CreateLoad(p0->getType(), p0, "m.a"), m0);
    b->CreateStore(b->CreateLoad(p1->getType(), p1, "m.b"), m1);
    b->CreateStore(b->CreateLoad(p2->getType(), p2, "m.c"), m2);
    return m;
}

::llvm::Value *LLVMCodegen::_make_float4x4(::llvm::Value *p0, ::llvm::Value *p1, ::llvm::Value *p2, ::llvm::Value *p3) noexcept {
    auto b = _current_context()->builder.get();
    auto t = _create_type(Type::of<float4x4>());
    auto m = b->CreateAlloca(t, nullptr, "float4x4.addr");
    auto m0 = b->CreateStructGEP(t, m, 0u, "float4x4.a");
    auto m1 = b->CreateStructGEP(t, m, 1u, "float4x4.b");
    auto m2 = b->CreateStructGEP(t, m, 2u, "float4x4.c");
    auto m3 = b->CreateStructGEP(t, m, 3u, "float4x4.d");
    b->CreateStore(b->CreateLoad(p0->getType(), p0, "m.a"), m0);
    b->CreateStore(b->CreateLoad(p1->getType(), p1, "m.b"), m1);
    b->CreateStore(b->CreateLoad(p2->getType(), p2, "m.c"), m2);
    b->CreateStore(b->CreateLoad(p3->getType(), p3, "m.d"), m3);
    return m;
}

luisa::string LLVMCodegen::_variable_name(Variable v) const noexcept {
    switch (v.tag()) {
        case Variable::Tag::LOCAL: return luisa::format("v{}.local", v.uid());
        case Variable::Tag::SHARED: return luisa::format("v{}.shared", v.uid());
        case Variable::Tag::REFERENCE: return luisa::format("v{}.ref", v.uid());
        case Variable::Tag::BUFFER: return luisa::format("v{}.buffer", v.uid());
        case Variable::Tag::TEXTURE: return luisa::format("v{}.texture", v.uid());
        case Variable::Tag::BINDLESS_ARRAY: return luisa::format("v{}.bindless", v.uid());
        case Variable::Tag::ACCEL: return luisa::format("v{}.accel", v.uid());
        case Variable::Tag::THREAD_ID: return "tid";
        case Variable::Tag::BLOCK_ID: return "bid";
        case Variable::Tag::DISPATCH_ID: return "did";
        case Variable::Tag::DISPATCH_SIZE: return "ls";
    }
    LUISA_ERROR_WITH_LOCATION("Invalid variable.");
}

}// namespace luisa::compute::llvm