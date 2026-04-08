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
#include "mozilla/Variant.h"
#include "mozilla/Vector.h"
#include "wasm/WasmModule.h"

namespace js {
namespace wasm {

enum class ComponentTypeKind {
  Bool = 0x7f,
  S8 = 0x7e,
  U8 = 0x7d,
  S16 = 0x7c,
  U16 = 0x7b,
  S32 = 0x7a,
  U32 = 0x79,
  S64 = 0x78,
  U64 = 0x77,
  F32 = 0x76,
  F64 = 0x75,
  Char = 0x74,
  String = 0x73,

  Record = 0x72,
  Variant = 0x71,
  List = 0x70,
  Tuple = 0x6f,
  Flags = 0x6e,
  Enum = 0x6d,
  Option = 0x6b,
  Result = 0x6a,
  Own = 0x69,
  Borrow = 0x68,

  FuncType = 0x40,  // async func types are not a separate kind
  ComponentType = 0x41,
  InstanceType = 0x42,
  ResourceType = 0x3f,  // resource types with callbacks are not a separate kind
};

inline bool ComponentTypeKindIsPrimitive(ComponentTypeKind kind) {
  return ComponentTypeKind::String <= kind && kind <= ComponentTypeKind::Bool;
}

inline bool ComponentTypeKindIsValueType(ComponentTypeKind kind) {
  return ComponentTypeKindIsPrimitive(kind) ||
         (ComponentTypeKind::Borrow <= kind &&
          kind <= ComponentTypeKind::Record &&
          int(kind) != 0x6c  // the one weird gap in the binary
         );
}

class ComponentValType {
  static constexpr uint32_t TypeIndexFlag = 1 << 31;
  uint32_t bits_;

  explicit ComponentValType(uint32_t bits) : bits_(bits) {}

 public:
  static ComponentValType primitive(ComponentTypeKind kind) {
    MOZ_ASSERT(ComponentTypeKindIsPrimitive(kind));
    return ComponentValType(uint32_t(kind));
  }
  static ComponentValType typeIndex(uint32_t idx) {
    MOZ_ASSERT(!(idx & TypeIndexFlag));
    return ComponentValType(TypeIndexFlag | idx);
  }

  bool isPrimitive() const { return !(bits_ & TypeIndexFlag); }
  bool isTypeIndex() const { return bits_ & TypeIndexFlag; }
  ComponentTypeKind asPrimitive() const {
    MOZ_ASSERT(isPrimitive());
    return ComponentTypeKind(bits_);
  }
  uint32_t asTypeIndex() const {
    MOZ_ASSERT(isTypeIndex());
    return bits_ & ~TypeIndexFlag;
  }
};
using ComponentValTypeVector =
    mozilla::Vector<ComponentValType, 0, SystemAllocPolicy>;

struct ComponentRecordField {
  CacheableName name;
  ComponentValType type;

  ComponentRecordField(CacheableName&& name_, ComponentValType type_)
      : name(std::move(name_)), type(type_) {}
};
using ComponentRecordFieldVector =
    mozilla::Vector<ComponentRecordField, 0, SystemAllocPolicy>;

struct ComponentVariantCase {
  CacheableName name;
  mozilla::Maybe<ComponentValType> type;
};
using ComponentVariantCaseVector =
    mozilla::Vector<ComponentVariantCase, 0, SystemAllocPolicy>;

struct ComponentResultType {
  mozilla::Maybe<ComponentValType> type;
  mozilla::Maybe<ComponentValType> errorType;
};

class ComponentDefType {
  ComponentTypeKind kind_;

  // TODO: Add component types, instance types, resource types?
  using TypeProps = mozilla::Variant<mozilla::Nothing,            // primitive,
                                     ComponentRecordFieldVector,  // record
                                     ComponentVariantCaseVector,  // variant
                                     ComponentValType,        // list, option
                                     ComponentValTypeVector,  // tuple
                                     CacheableNameVector,     // flags, enum
                                     ComponentResultType,     // result
                                     uint32_t                 // own, borrow
                                     >;
  TypeProps props_;

  explicit ComponentDefType(ComponentTypeKind kind)
      : kind_(kind), props_(mozilla::Nothing()) {}
  explicit ComponentDefType(ComponentRecordFieldVector&& fields)
      : kind_(ComponentTypeKind::Record), props_(std::move(fields)) {}

 public:
  static ComponentDefType primitive(ComponentTypeKind kind) {
    MOZ_ASSERT(ComponentTypeKindIsPrimitive(kind));
    return ComponentDefType(kind);
  }
  static ComponentDefType record(ComponentRecordFieldVector&& fields) {
    return ComponentDefType(std::move(fields));
  }

  ComponentTypeKind kind() const { return kind_; }
};

struct CoreInstanceDesc {};

class ComponentExternDesc {
  ComponentSort sort_;

  // Used for kinds CoreModule, Component, Instance, and the `eq` case of Type.
  uint32_t typeIndex_;

  explicit ComponentExternDesc(ComponentSort sort) : sort_(sort) {
    MOZ_ASSERT(ComponentSortValidForExternDesc(sort));
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
  using TypeVector = mozilla::Vector<ComponentDefType, 0, SystemAllocPolicy>;

  // JS API and JS::WasmComponent implementation:
  JSObject* createObject(JSContext* cx) const override;
  JSObject* createObjectForAsmJS(JSContext* cx) const override;

 public:
  ModuleVector modules;
  TypeVector types;
  ComponentExportVector exports;
};

using MutableComponent = RefPtr<Component>;
using SharedComponent = RefPtr<const Component>;

}  // namespace wasm
}  // namespace js

#endif