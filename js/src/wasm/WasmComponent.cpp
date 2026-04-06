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

ComponentExternDesc ComponentExport::implicitExternDesc(Component& c) {}

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