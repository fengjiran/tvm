/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file codegen_c_host.cc
 */
#include "codegen_c_host.h"

#include <tvm/ffi/reflection/registry.h>
#include <tvm/runtime/module.h>
#include <tvm/target/codegen.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tvm {
namespace codegen {

CodeGenCHost::CodeGenCHost() { module_name_ = name_supply_->FreshName("__tvm_ffi_library_ctx"); }

void CodeGenCHost::Init(bool output_ssa, bool emit_asserts, bool emit_fwd_func_decl,
                        std::string target_str, const std::unordered_set<std::string>& devices) {
  emit_asserts_ = emit_asserts;
  emit_fwd_func_decl_ = emit_fwd_func_decl;
  declared_globals_.clear();
  decl_stream << "// tvm target: " << target_str << "\n";
  decl_stream << "#define TVM_EXPORTS\n";
  decl_stream << "#include \"tvm/runtime/base.h\"\n";
  decl_stream << "#include \"tvm/runtime/c_backend_api.h\"\n";
  decl_stream << "#include \"tvm/ffi/c_api.h\"\n";
  decl_stream << "#include <math.h>\n";
  decl_stream << "#include <stdbool.h>\n";
  CodeGenCHost::InitGlobalContext();
  CodeGenC::Init(output_ssa);
}

void CodeGenCHost::InitGlobalContext() {
  decl_stream << "void* " << tvm::runtime::symbol::tvm_ffi_library_ctx << " = NULL;\n";
}

void CodeGenCHost::DefineModuleName() { decl_stream << "void* " << module_name_ << " = NULL;\n"; }

void CodeGenCHost::AddFunction(const GlobalVar& gvar, const PrimFunc& func) {
  return AddFunction(gvar, func, /*emit_fwd_func_decl=*/false);
}

void CodeGenCHost::AddFunction(const GlobalVar& gvar, const PrimFunc& func,
                               bool emit_fwd_func_decl) {
  auto global_symbol = func->GetAttr<String>(tvm::attr::kGlobalSymbol);
  if (global_symbol) {
    function_names_.push_back(global_symbol.value());
  }

  emit_fwd_func_decl_ = emit_fwd_func_decl;
  CodeGenC::AddFunction(gvar, func);
  if (func->HasNonzeroAttr(tir::attr::kIsEntryFunc)) {
    ICHECK(global_symbol.has_value())
        << "CodeGenCHost: The entry func must have the global_symbol attribute, "
        << "but function " << gvar << " only has attributes " << func->attrs;

    function_names_.push_back(runtime::symbol::tvm_module_main);
    stream << "// CodegenC: NOTE: Auto-generated entry function\n";
    PrintFuncPrefix(stream);
    PrintType(func->ret_type, stream);
    stream << " " << tvm::runtime::symbol::tvm_module_main
           << "(void* self, void* args,int num_args, void* result) {\n";
    stream << "  return " << global_symbol.value() << "(self, args, num_args, result);\n";
    stream << "}\n";
  }
}

void CodeGenCHost::GenerateForwardFunctionDeclarations(String global_symbol,
                                                       const Array<Type>& arg_types,
                                                       const Type& ret_type) {
  if (!emit_fwd_func_decl_) {
    return;
  }
  for (auto& func_already_defined : GetFunctionNames()) {
    if (global_symbol == func_already_defined) {
      return;
    }
  }
  this->PrintFuncPrefix(fwd_decl_stream);
  this->PrintType(ret_type, fwd_decl_stream);
  fwd_decl_stream << " " << global_symbol << "(";
  for (size_t i = 0; i < arg_types.size(); ++i) {
    if (i > 0) {
      fwd_decl_stream << ", ";
    }
    CodeGenSourceBase::PrintType(arg_types[i], fwd_decl_stream);
  }
  fwd_decl_stream << ");\n";
}

void CodeGenCHost::PrintFuncPrefix(std::ostream& os) {  // NOLINT(*)
  os << "#ifdef __cplusplus\n"
     << "extern \"C\"\n"
     << "#endif\n"
     << "TVM_DLL ";
}

void CodeGenCHost::PrintType(DataType t, std::ostream& os) {  // NOLINT(*)
  int lanes = t.lanes();
  if (t.is_handle()) {
    ICHECK_EQ(lanes, 1) << "does not support vector types";
    os << "void*";
    return;
  }
  if (t.is_void()) {
    os << "void";
    return;
  }
  if (t == DataType::Bool()) {
    os << "bool";
    return;
  }
  bool fail = false;
  if (t.is_float()) {
    switch (t.bits()) {
      case 16:
        os << "half";
        break;
      case 32:
        os << "float";
        break;
      case 64:
        os << "double";
        break;
      default:
        fail = true;
        break;
    }
    if (!fail && lanes == 1) return;
    if (!fail && (lanes >= 2 && lanes <= 16)) {
      os << lanes;
      return;
    }
  } else if (t.is_uint() || t.is_int()) {
    if (t.is_uint()) {
      os << 'u';
    }
    switch (t.bits()) {
      case 8:
        os << "int8_t";
        break;
      case 16:
        os << "int16_t";
        break;
      case 32:
        os << "int32_t";
        break;
      case 64:
        os << "int64_t";
        break;
      case 1:
        os << "int32_t";
        break;
      default:
        fail = true;
        break;
    }
    if (!fail && lanes == 1) return;
    if (!fail && (lanes >= 2 && lanes <= 16)) {
      os << lanes;
      return;
    }
  }
  LOG(FATAL) << "Cannot convert type " << t << " to C type";
}

void CodeGenCHost::VisitExpr_(const BroadcastNode* op, std::ostream& os) {  // NOLINT(*)
  std::string v = PrintExpr(op->value);
  int lanes = op->dtype.lanes();
  os << "((";
  PrintType(op->dtype, os);
  os << ")(";
  for (int i = 0; i < lanes; ++i) {
    if (i != 0) os << ", ";
    os << v;
  }
  os << "))";
}

void CodeGenCHost::PrintGetFuncFromBackend(const std::string& func_name,
                                           const std::string& packed_func_name) {
  this->PrintIndent();
  this->stream << "if (" << packed_func_name << " == NULL) {\n";
  int packed_func_if_scope = this->BeginScope();
  this->PrintIndent();
  this->stream << "if (TVMBackendGetFuncFromEnv(" << module_name_ << ", \"" << func_name << "\""
               << ", &" << packed_func_name << ") != 0) {\n";
  int get_func_env_scope = this->BeginScope();
  this->PrintIndent();
  this->stream << "return -1;\n";
  this->EndScope(get_func_env_scope);
  this->PrintIndent();
  this->stream << "}\n";
  this->EndScope(packed_func_if_scope);
  this->PrintIndent();
  this->stream << "}\n";
}

void CodeGenCHost::PrintCallPacked(const CallNode* op) {
  const StringImmNode* func_name = op->args[0].as<StringImmNode>();
  ICHECK(func_name != nullptr)
      << "tvm_call_[c]packed_lowered expects first argument as function name";
  int64_t begin = op->args[2].as<IntImmNode>()->value;
  int64_t end = op->args[3].as<IntImmNode>()->value;
  int64_t num_args = end - begin;
  ICHECK_GE(num_args, 0);

  std::string packed_func_name;
  if (op->op.same_as(builtin::tvm_call_packed_lowered())) {
    packed_func_name = GetPackedName(op);
    this->PrintGetFuncFromBackend(func_name->value, packed_func_name);
  } else {
    // directly use the original symbol
    ICHECK(op->op.same_as(builtin::tvm_call_cpacked_lowered()));
    packed_func_name = func_name->value;
  }

  std::string args_stack = PrintExpr(op->args[1]);
  this->PrintIndent();
  std::string result = name_supply_->FreshName("result");
  this->stream << "TVMFFIAny " << result << ";\n";
  this->PrintIndent();
  // must make sure type_index is set to none
  this->stream << result << ".type_index = kTVMFFINone;\n";
  this->PrintIndent();
  this->stream << result << ".v_int64 = 0;\n";
  this->PrintIndent();
  if (op->op.same_as(builtin::tvm_call_packed_lowered())) {
    this->stream << "if (TVMFFIFunctionCall(" << packed_func_name << ", ";
  } else {
    this->stream << "if (" << packed_func_name << "(NULL, ";
  }
  this->stream << "(TVMFFIAny*) " << args_stack << ", " << num_args << ", "
               << "&" << result << ") != 0) {\n";
  int func_call_scope = this->BeginScope();
  this->PrintIndent();
  this->stream << "return -1;\n";
  this->EndScope(func_call_scope);
  this->PrintIndent();
  this->stream << "}\n";
}

std::string CodeGenCHost::GetPackedName(const CallNode* op) {
  const StringImmNode* s = op->args[0].as<StringImmNode>();
  ICHECK(s != nullptr) << "tvm_call_packed_lowered expects first argument as function name";
  std::string func_name = s->value;
  std::string packed_func_name = func_name + "_packed";
  std::string unique_name;
  auto it = declared_globals_.find(packed_func_name);
  if (it != declared_globals_.end()) {
    unique_name = it->second;
  } else {
    unique_name = name_supply_->FreshName(packed_func_name);
    declared_globals_[packed_func_name] = unique_name;
    decl_stream << "static void* " << unique_name << " = NULL;\n";
  }
  return unique_name;
}

void CodeGenCHost::VisitExpr_(const CallNode* op, std::ostream& os) {  // NOLINT(*)
  if (op->op.same_as(builtin::tvm_stack_alloca())) {
    std::string stack_name = name_supply_->FreshName("stack");
    const std::string& type = op->args[0].as<StringImmNode>()->value;
    const IntImmNode* num = op->args[1].as<IntImmNode>();
    ICHECK(num != nullptr);
    static_assert(alignof(TVMFFIAny) % alignof(DLTensor) == 0, "invariant");
    size_t unit = sizeof(TVMFFIAny);
    size_t size = 0;
    if (type == "shape") {
      size = (num->value * sizeof(ffi::Shape::index_type) + unit - 1) / unit;
    } else if (type == "tvm_ffi_any") {
      size = (num->value * sizeof(TVMFFIAny) + unit - 1) / unit;
    } else if (type == "array") {
      size = (num->value * sizeof(DLTensor) + unit - 1) / unit;
    } else {
      LOG(FATAL) << "Unknown stack alloca type " << type;
    }
    this->PrintIndent();
    this->stream << "TVMFFIAny " << stack_name << "[" << size << "];\n";
    os << stack_name;
  } else if (op->op.same_as(builtin::tvm_call_packed_lowered())) {
    this->PrintCallPacked(op);
  } else if (op->op.same_as(builtin::tvm_call_cpacked_lowered())) {
    this->PrintCallPacked(op);
  } else if (op->op.same_as(builtin::tvm_throw_last_error())) {
    this->PrintIndent();
    this->stream << "return -1;\n";
  } else {
    CodeGenC::VisitExpr_(op, os);
  }
}

void CodeGenCHost::VisitStmt_(const AssertStmtNode* op) {  // NOLINT(*)
  if (emit_asserts_) {
    std::string cond = PrintExpr(op->condition);
    PrintIndent();
    stream << "if (!(" << cond << ")) {\n";
    int assert_if_scope = this->BeginScope();
    PrintIndent();
    stream << "TVMFFIErrorSetRaisedFromCStr(\"RuntimeError\", \""
           << op->message.as<StringImmNode>()->value << "\", NULL);\n";
    PrintIndent();
    stream << "return -1;\n";
    this->EndScope(assert_if_scope);
    PrintIndent();
    stream << "}\n";
  }
  this->PrintStmt(op->body);
}

void CodeGenCHost::VisitExpr_(const MinNode* op, std::ostream& os) {  // NOLINT(*)
  PrintTernaryCondExpr(op, "<", os);
}

void CodeGenCHost::VisitExpr_(const MaxNode* op, std::ostream& os) {  // NOLINT(*)
  PrintTernaryCondExpr(op, ">", os);
}

template <typename T>
inline void CodeGenCHost::PrintTernaryCondExpr(const T* op, const char* compare,
                                               std::ostream& os) {  // NOLINT(*)
  std::ostringstream temp_a;
  VisitExpr(op->a, temp_a);
  std::string a_id = SSAGetID(temp_a.str(), op->a.dtype());
  std::ostringstream temp_b;
  VisitExpr(op->b, temp_b);
  std::string b_id = SSAGetID(temp_b.str(), op->b.dtype());

  os << "((" << a_id << ") " << compare << " (" << b_id << ") "
     << "? (" << a_id << ") : (" << b_id << "))";
}

runtime::Module BuildCHost(IRModule mod, Target target) {
  bool output_ssa = false;
  bool emit_asserts = false;
  bool emit_fwd_func_decl = true;

  std::unordered_set<std::string> devices;
  if (mod->GetAttr<Map<GlobalVar, String>>("device_contexts") != nullptr) {
    Map<GlobalVar, String> device_contexts =
        mod->GetAttr<Map<GlobalVar, String>>("device_contexts").value();
    for (auto const& context : device_contexts) {
      devices.insert(context.second.data());
    }
  }

  CodeGenCHost cg;
  cg.Init(output_ssa, emit_asserts, emit_fwd_func_decl, target->str(), devices);
  cg.SetConstantsByteAlignment(target->GetAttr<Integer>("constants-byte-alignment").value_or(16));

  auto is_aot_executor_fn = [](const PrimFunc& func) -> bool {
    return func->GetAttr<Bool>("runner_function", Bool(false)).value();
  };

  std::vector<std::pair<GlobalVar, PrimFunc>> funcs;
  for (auto [gvar, base_func] : mod->functions) {
    ICHECK(base_func->IsInstance<PrimFuncNode>()) << "CodegenCHost: Can only take PrimFunc";
    auto prim_func = Downcast<PrimFunc>(base_func);
    funcs.push_back({gvar, prim_func});
  }

  // Sort functions
  auto sort_key = [&is_aot_executor_fn](const auto& kv) {
    return std::tuple{is_aot_executor_fn(kv.second), kv.first->name_hint};
  };
  std::sort(funcs.begin(), funcs.end(), [&sort_key](const auto& kv_a, const auto& kv_b) {
    return sort_key(kv_a) < sort_key(kv_b);
  });

  for (const auto& [gvar, prim_func] : funcs) {
    cg.DeclareFunction(gvar, prim_func);
  }

  // Codegen all functions.  Passing emit_fwd_func_decl=true adds a
  // forward declaration for any `builtin::call_extern`, based on the
  // arguments provided to it.
  for (const auto& [gvar, prim_func] : funcs) {
    cg.AddFunction(gvar, prim_func, emit_fwd_func_decl);
  }

  std::string code = cg.Finish();
  return CSourceModuleCreate(code, "c", cg.GetFunctionNames());
}

TVM_FFI_STATIC_INIT_BLOCK({
  namespace refl = tvm::ffi::reflection;
  refl::GlobalDef().def("target.build.c", BuildCHost);
});
}  // namespace codegen
}  // namespace tvm
