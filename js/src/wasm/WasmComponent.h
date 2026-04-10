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

// A "sort", or "kind", of item in the component model, used for all cases where
// we must refer to a different item.
//
// This type is also used for the `externdesc` type, which describes what
// components (not core modules) can import and export, and whose cases are a
// subset of `sort`. Sorts that are invalid for `externdesc` have the highest
// bit set. Additionally, sorts that can be exported by core modules (core:sort)
// have the second-highest bit set.
enum class ComponentSort : uint8_t {
  Func = 0x01,
  Value = 0x02,
  Type = 0x03,
  Component = 0x04,
  Instance = 0x05,

  CoreFunction = 0xc0 | int(DefinitionKind::Function),
  CoreTable = 0xc0 | int(DefinitionKind::Table),
  CoreMemory = 0xc0 | int(DefinitionKind::Memory),
  CoreGlobal = 0xc0 | int(DefinitionKind::Global),
  CoreTag = 0xc0 | int(DefinitionKind::Tag),

  CoreType = 0x80 | 0x10,
  CoreModule = 0x11,
  CoreInstance = 0x80 | 0x12,
};

static inline bool ComponentSortValidForExternDesc(ComponentSort sort) {
  return (uint8_t(sort) & 0x80) == 0;
}

static inline bool ComponentSortIsCoreSort(ComponentSort sort) {
  return (uint8_t(sort) & 0x40) != 0;
}

static inline DefinitionKind CoreSortFromComponentSort(ComponentSort sort) {
  return DefinitionKind(uint8_t(sort) & ~0xc0);
}

enum class ComponentTypeKind : uint8_t {
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

  Func = 0x40,  // async func types are not a separate kind
  Component = 0x41,
  Instance = 0x42,
  Resource = 0x3f,  // resource types with callbacks are not a separate kind
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

struct ComponentFuncType {
  ComponentValTypeVector paramTypes;
  CacheableNameVector paramNames;
  mozilla::Maybe<ComponentValType> resultType;
  bool isAsync;
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
                                     uint32_t,                // own, borrow

                                     ComponentFuncType  // func
                                     >;
  TypeProps props_;

  explicit ComponentDefType(ComponentTypeKind kind)
      : kind_(kind), props_(mozilla::Nothing()) {}
  explicit ComponentDefType(ComponentRecordFieldVector&& fields)
      : kind_(ComponentTypeKind::Record), props_(std::move(fields)) {}
  explicit ComponentDefType(ComponentFuncType&& funcType)
      : kind_(ComponentTypeKind::Func), props_(std::move(funcType)) {}

 public:
  static ComponentDefType primitive(ComponentTypeKind kind) {
    MOZ_ASSERT(ComponentTypeKindIsPrimitive(kind));
    return ComponentDefType(kind);
  }
  static ComponentDefType record(ComponentRecordFieldVector&& fields) {
    return ComponentDefType(std::move(fields));
  }
  static ComponentDefType func(ComponentFuncType&& ft) {
    return ComponentDefType(std::move(ft));
  }

  ComponentTypeKind kind() const { return kind_; }
};

class ComponentAlias {
  // For export aliases, the index of the component instance or core instance.
  // For outer aliases, the number of enclosing components to jump out to.
  uint32_t instanceIdx_;

  // The index of the aliased item in its component instance or core instance.
  uint32_t innerIdx_;

  // Whether the alias is to be interpreted as an outer alias.
  bool isOuter_;

  // Whether `instanceIdx` refers to a core instance or component instance.
  bool isCoreInstance_;

  // The sort of item being aliased.
  ComponentSort sort_;

  explicit ComponentAlias(uint32_t instanceIdx, uint32_t innerIdx,
                          ComponentSort sort, bool isOuter, bool isCoreInstance)
      : instanceIdx_(instanceIdx),
        innerIdx_(innerIdx),
        isOuter_(isOuter),
        isCoreInstance_(isCoreInstance),
        sort_(sort) {}

 public:
  static ComponentAlias fromExport(uint32_t instanceIdx, uint32_t innerIdx,
                                   ComponentSort sort) {
    MOZ_ASSERT(!ComponentSortIsCoreSort(sort));
    return ComponentAlias(instanceIdx, innerIdx, sort, /*isOuter=*/false,
                          /*isCoreInstance=*/false);
  }
  static ComponentAlias fromCoreExport(uint32_t instanceIdx, uint32_t innerIdx,
                                       ComponentSort sort) {
    MOZ_ASSERT(ComponentSortIsCoreSort(sort));
    return ComponentAlias(instanceIdx, innerIdx, sort, /*isOuter=*/false,
                          /*isCoreInstance=*/true);
  }
  static ComponentAlias outer(uint32_t count, uint32_t index,
                              ComponentSort sort) {
    MOZ_ASSERT(!ComponentSortIsCoreSort(sort));
    return ComponentAlias(count, index, sort, /*isOuter=*/true,
                          /*isCoreInstance=*/false);
  }

  bool isExport() const { return !isOuter_ && !isCoreInstance_; }
  bool isCoreExport() const { return !isOuter_ && isCoreInstance_; }
  bool isOuter() const {
    MOZ_ASSERT(!isCoreInstance_);
    return isOuter_;
  }

  ComponentSort sort() const { return sort_; }
  uint32_t instanceIdx() const { return instanceIdx_; }
  uint32_t itemIndex() const { return innerIdx_; }
};

struct ComponentCanonOpt {
  // TODO
};

using ComponentCanonOptVector =
    mozilla::Vector<ComponentCanonOpt, 0, SystemAllocPolicy>;

struct ComponentLiftedFuncDesc {
  // TODO: Actually store something useful here. I'm not sure at the moment if
  // it makes sense to store the raw index, options, and dest type, or to store
  // some kind of new value here. It will all depend on what instantiation
  // actually looks like. So not touching it for now.
};

struct CoreInstanceInstantiateArg {
  CacheableName name;
  uint32_t instanceIdx;
};

using CoreInstanceInstantiateArgVector =
    mozilla::Vector<CoreInstanceInstantiateArg, 0, SystemAllocPolicy>;

// Instructions for instantiating a core instance from a core module,
// corresponding to this text production:
//
//     (core instance (instantiate <modidx>) (with ...)*)`
//
struct CoreInstanceDescFromModule {
  // The core module to instantiate.
  uint32_t moduleIndex;

  // The instance's "with" declarations. In the binary format there is no inline
  // export form, only a form that uses the exports of another core instance.
  CoreInstanceInstantiateArgVector args;
};

// Instructions for instantiating a core instance by re-exporting core items
// already present in the component's index spaces. Corresponds to this text:
//
//     (core instance (export ...)*)
//
// This form of core instantiation semantically creates a new anonymous module
// which imports the given definitions and re-exports them. Alternatively, you
// can consider it a mere renaming of the items exported by other modules, but
// creating an anonymous module simplifies our implementation. Note that the
// module does not live in the component's core module index space.
//
// TODO: Fill this out and figure out how to satisfy the module's imports.
struct CoreInstanceDescFromInlineExports {
  SharedModule mod;
};

// Instructions for instantiating a core instance.
using CoreInstanceDesc = mozilla::Variant<CoreInstanceDescFromModule,
                                          CoreInstanceDescFromInlineExports>;

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

  ComponentSort sort() const { return sort_; }
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

class Component : public JS::WasmComponent {
  using CoreModuleVector = mozilla::Vector<SharedModule, 0, SystemAllocPolicy>;
  using CoreInstanceVector =
      mozilla::Vector<CoreInstanceDesc, 0, SystemAllocPolicy>;
  using TypeVector = mozilla::Vector<ComponentDefType, 0, SystemAllocPolicy>;
  using FuncVector =
      mozilla::Vector<ComponentLiftedFuncDesc, 0, SystemAllocPolicy>;
  using ExportVector = Vector<ComponentExport, 0, SystemAllocPolicy>;
  using AliasVector = Vector<ComponentAlias, 0, SystemAllocPolicy>;

  // JS API and JS::WasmComponent implementation:
  JSObject* createObject(JSContext* cx) const override;
  JSObject* createObjectForAsmJS(JSContext* cx) const override;

 public:
  CoreModuleVector coreModules;
  CoreInstanceVector coreInstances;
  TypeVector types;
  FuncVector funcs;
  ExportVector exports;

  AliasVector coreFuncs;  // TODO: This will have to accommodate lowered funcs
  AliasVector coreTables;
  AliasVector coreMemories;
  AliasVector coreGlobals;
  AliasVector coreTags;

  SharedModule moduleForCoreInstance(uint32_t instanceIdx) {
    CoreInstanceDesc& instance = coreInstances[instanceIdx];

    return instance.match(
        [&coreModules = this->coreModules](CoreInstanceDescFromModule& desc) {
          return coreModules[desc.moduleIndex];
        },
        [](CoreInstanceDescFromInlineExports& desc) { return desc.mod; });
  }
};

using MutableComponent = RefPtr<Component>;
using SharedComponent = RefPtr<const Component>;

}  // namespace wasm
}  // namespace js

#endif