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

#ifndef wasm_component_h
#define wasm_component_h

#include "js/WasmComponent.h"

#include "mozilla/RefPtr.h"
#include "mozilla/Vector.h"
#include "wasm/WasmModule.h"

namespace js {
namespace wasm {

struct CoreInstanceDesc {};

class ComponentExternDesc {
  ComponentSort kind_;

  // Used for kinds CoreModule, Component, Instance, and the `eq` case of Type.
  uint32_t typeIndex_;

  explicit ComponentExternDesc(ComponentSort kind) : kind_(kind) {
    MOZ_ASSERT(ComponentSortValidForExternDesc(kind));
  }

 public:
  ComponentExternDesc() = default;

  static ComponentExternDesc func(uint32_t funcIdx) {
    ComponentExternDesc desc(ComponentSort::Func);
    desc.typeIndex_ = funcIdx;
    return desc;
  }
  static ComponentExternDesc coreModule(uint32_t typeIdx) {
    ComponentExternDesc desc(ComponentSort::CoreModule);
    desc.typeIndex_ = typeIdx;
    return desc;
  }
};

class ComponentExport {
 public:
  struct CacheablePod {
    ComponentSort sort_;
    uint32_t index_;

    WASM_CHECK_CACHEABLE_POD(sort_, index_);
  };

 private:
  CacheableName name_;
  CacheableName versionSuffix_;
  CacheablePod pod;

 public:
  ComponentExport() = default;
  explicit ComponentExport(CacheableName&& name, uint32_t index,
                           ComponentSort sort, CacheableName&& versonSuffix);

  ComponentExternDesc implicitExternDesc(Component& c);
};

using ComponentExportVector = Vector<ComponentExport, 0, SystemAllocPolicy>;

class Component : public JS::WasmComponent {
  using ModuleVector = mozilla::Vector<SharedModule, 0, SystemAllocPolicy>;

  // JS API and JS::WasmComponent implementation:
  JSObject* createObject(JSContext* cx) const override;
  JSObject* createObjectForAsmJS(JSContext* cx) const override;

 public:
  ModuleVector modules;
  ComponentExportVector exports;
};

using MutableComponent = RefPtr<Component>;
using SharedComponent = RefPtr<const Component>;

}  // namespace wasm
}  // namespace js

#endif