/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 *
 * Copyright 2015 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wasm/WasmComponent.h"

#include "js/experimental/TypedData.h"  // JS_NewUint8Array
#include "js/friend/ErrorMessages.h"    // js::GetErrorMessage, JSMSG_*
#include "js/PropertyAndElement.h"  // JS_DefineProperty, JS_DefinePropertyById
#include "vm/GlobalObject.h"
#include "vm/PlainObject.h"  // js::PlainObject
#include "vm/Warnings.h"     // WarnNumberASCII
#include "wasm/WasmJS.h"

using namespace js;
using namespace js::wasm;

ComponentExport::ComponentExport(CacheableName&& fieldName, uint32_t index,
                                 ComponentSort sort,
                                 CacheableName&& versionSuffix)
    : name_(std::move(fieldName)), versionSuffix_(std::move(versionSuffix)) {
  pod.sort_ = sort;
  pod.index_ = index;
}

ComponentExternDesc ComponentExport::implicitExternDesc(Component& c) {
  MOZ_CRASH("TODO");
}

mozilla::Maybe<FuncType> wasm::FlattenFuncType(const Component& c,
                                               const ComponentFuncType& ft) {
  ValTypeVector params;
  ValTypeVector results;

  if (!FlattenTypes(c, ft.paramTypes, &params)) {
    return mozilla::Nothing();
  }
  if (ft.resultType.isSome()) {
    if (!FlattenType(c, ft.resultType.ref(), &results)) {
      return mozilla::Nothing();
    }
  }

  return mozilla::Some(FuncType(std::move(params), std::move(results)));
}

bool wasm::FlattenTypes(const Component& c, const ComponentValTypeVector& ts,
                        ValTypeVector* result) {
  // Pre-reserve at least enough space for a bunch of primitives. We still may
  // exceed the capacity reserved here but at least we can avoid a little bit of
  // allocation. (Appends after this point are not to be considered infallible.)
  if (!result->reserve(ts.length())) {
    return false;
  }

  for (const ComponentValType& t : ts) {
    if (!FlattenType(c, t, result)) {
      return false;
    }
  }

  return true;
}

bool wasm::FlattenType(const Component& c, const ComponentValType& t,
                       ValTypeVector* result) {
  ComponentTypeKind kind;
  if (t.isTypeIndex()) {
    kind = c.types[t.asTypeIndex()].kind();
  } else if (t.isPrimitive()) {
    kind = t.asPrimitive();
  } else {
    MOZ_CRASH();
  }

  switch (kind) {
    // Simple primitives
    case ComponentTypeKind::Bool:
    case ComponentTypeKind::U8:
    case ComponentTypeKind::U16:
    case ComponentTypeKind::U32:
    case ComponentTypeKind::S8:
    case ComponentTypeKind::S16:
    case ComponentTypeKind::S32:
    case ComponentTypeKind::Char:
    case ComponentTypeKind::Flags:
    case ComponentTypeKind::Own:
    case ComponentTypeKind::Borrow: {
      if (!result->append(ValType::i32())) {
        return false;
      }
    } break;
    case ComponentTypeKind::U64:
    case ComponentTypeKind::S64: {
      if (!result->append(ValType::i64())) {
        return false;
      }
    } break;
    case ComponentTypeKind::F32: {
      if (!result->append(ValType::f32())) {
        return false;
      }
    } break;
    case ComponentTypeKind::F64: {
      if (!result->append(ValType::f64())) {
        return false;
      }
    } break;

    // Strings are always two i32's
    case ComponentTypeKind::String: {
      if (!result->append(ValType::i32())) {
        return false;
      }
      if (!result->append(ValType::i32())) {
        return false;
      }
    } break;

    // Compound types have dedicated logic. Note that our data storage for some
    // types disagrees with the categories in the canonical ABI explainer, e.g.
    // we represent tuples as a vector of value types, not a record.
    case ComponentTypeKind::List: {
      // This will have to change when support is added for fixed-length lists.
      if (!result->append(ValType::i32())) {
        return false;
      }
      if (!result->append(ValType::i32())) {
        return false;
      }
    } break;
    case ComponentTypeKind::Record: {
      const ComponentRecordFieldVector& fields =
          c.types[t.asTypeIndex()].asRecord();
      if (!FlattenRecord(c, fields, result)) {
        return false;
      }
    } break;
    case ComponentTypeKind::Tuple: {
      const ComponentValTypeVector& types = c.types[t.asTypeIndex()].asTuple();
      if (!FlattenTypes(c, types, result)) {
        return false;
      }
    } break;
    case ComponentTypeKind::Variant:
    case ComponentTypeKind::Enum:
    case ComponentTypeKind::Option:
    case ComponentTypeKind::Result: {
      MOZ_CRASH("TODO");
    } break;

    case ComponentTypeKind::Component:
    case ComponentTypeKind::Func:
    case ComponentTypeKind::Instance:
    case ComponentTypeKind::Resource: {
      MOZ_CRASH("should have been rejected when the func type was validated");
    } break;
  }

  return true;
}

bool wasm::FlattenRecord(const Component& c,
                         const ComponentRecordFieldVector& fields,
                         ValTypeVector* result) {
  for (const ComponentRecordField& field : fields) {
    if (!FlattenType(c, field.type, result)) {
      return false;
    }
  }

  return true;
}

/* virtual */
JSObject* Component::createObject(JSContext* cx) const {
  if (!GlobalObject::ensureConstructor(cx, cx->global(), JSProto_WebAssembly)) {
    return nullptr;
  }

  // TODO: Is this all applicable to components? If so, can we unify it with
  // modules?
  JS::RootedVector<JSString*> parameterStrings(cx);
  JS::RootedVector<Value> parameterArgs(cx);
  bool canCompileStrings = false;
  if (!cx->isRuntimeCodeGenEnabled(JS::RuntimeCode::WASM, nullptr,
                                   JS::CompilationType::Undefined,
                                   parameterStrings, nullptr, parameterArgs,
                                   NullHandleValue, &canCompileStrings)) {
    return nullptr;
  }
  if (!canCompileStrings) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_CSP_BLOCKED_WASM, "WebAssembly.Component");
    return nullptr;
  }

  RootedObject proto(cx, &cx->global()->getPrototype(JSProto_WasmComponent));
  return WasmComponentObject::create(cx, *this, proto);
}

/* virtual */
JSObject* Component::createObjectForAsmJS(JSContext* cx) const {
  // Use nullptr to get the default object prototype. These objects are never
  // exposed to script for asm.js.
  MOZ_CRASH();
}