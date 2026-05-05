/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef wasm_component_h
#define wasm_component_h

#ifdef ENABLE_WASM_COMPONENTS

#  include "js/WasmComponent.h"

#  include "mozilla/Atomics.h"
#  include "mozilla/HashTable.h"
#  include "mozilla/Maybe.h"
#  include "mozilla/RefPtr.h"
#  include "mozilla/Span.h"
#  include "mozilla/Variant.h"
#  include "mozilla/Vector.h"
#  include "wasm/WasmModule.h"

namespace js {
namespace wasm {

using ComponentString = mozilla::Span<const char>;
using ComponentStringVector =
    mozilla::Vector<ComponentString, 0, SystemAllocPolicy>;

// A helper macro allowing ComponentStrings to be printed with `%.*s`.
#  define ComponentString_Printf(n) (int)(n).Length(), (n).data()

// A "sort", or "kind", of item in the component model, used for all cases where
// we must refer to a different item.
//
// This type is also used for the `externdesc` type, which describes what
// components (not core modules) can import and export, and whose cases are a
// subset of `sort`. Sorts that are valid for `externdesc` have the highest bit
// set. Additionally, sorts that can be exported by core modules (core:sort)
// have the second-highest bit set, and correspond to wasm::DefinitionKind.
enum class ComponentSort : uint8_t {
  Invalid = 0,

  Func = 0x80 | 0x01,
  Type = 0x80 | 0x03,
  Component = 0x80 | 0x04,
  Instance = 0x80 | 0x05,

  CoreFunction = 0x40 | int(DefinitionKind::Function),
  CoreTable = 0x40 | int(DefinitionKind::Table),
  CoreMemory = 0x40 | int(DefinitionKind::Memory),
  CoreGlobal = 0x40 | int(DefinitionKind::Global),
  CoreTag = 0x40 | int(DefinitionKind::Tag),

  CoreType = 0x10,
  CoreModule = 0x80 | 0x11,
  CoreInstance = 0x12,

  // Type bounds
  Eq = 0x20,
  SubResource = 0x21,
};

// Checks if the given sort is valid for a component import or export (the
// component `externdesc` type).
inline bool ComponentSortValidForExternDesc(ComponentSort sort) {
  return (uint8_t(sort) & 0x80) != 0;
}

// Checks if the given sort is for a core item that can be imported or exported,
// i.e. a DefinitionKind imported into the component model. To extract the
// underlying DefinitionKind, use CoreSortFromComponentSort.
inline bool ComponentSortIsCoreSort(ComponentSort sort) {
  return (uint8_t(sort) & 0x40) != 0;
}

// Extracts the underlying DefinitionKind from a ComponentSort (if there is
// one).
inline DefinitionKind CoreSortFromComponentSort(ComponentSort sort) {
  MOZ_ASSERT(ComponentSortIsCoreSort(sort));
  return DefinitionKind(uint8_t(sort) & ~0xc0);
}

// Every kind of type that can be defined in the component model. Not all types
// are valid in all contexts.
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

  // Type bounds
  Eq = 0x20,
  SubResource = 0x21,

  // Convenience for ComponentTypeKindIsPrimitive. "First" and "last" refer to
  // the actual byte value.
  FirstPrimitive = String,
  LastPrimitive = Bool,
};

// Checks if the given kind is for a primitive type (`primvaltype`), i.e. one
// that doesn't need to be defined and referenced.
inline bool ComponentTypeKindIsPrimitive(ComponentTypeKind kind) {
  return ComponentTypeKind::FirstPrimitive <= kind &&
         kind <= ComponentTypeKind::LastPrimitive;
}

// Checks if the given kind is for a value type (`valtype`), i.e. one that can
// be used for function parameters.
inline bool ComponentTypeKindIsValueType(ComponentTypeKind kind) {
  return ComponentTypeKindIsPrimitive(kind) ||
         (ComponentTypeKind::Borrow <= kind &&
          kind <= ComponentTypeKind::Record &&
          int(kind) != 0x6c  // the one weird gap in the binary
         );
}

inline bool ComponentTypeKindIsTypeBound(ComponentTypeKind kind) {
  return kind == ComponentTypeKind::Eq ||
         kind == ComponentTypeKind::SubResource;
}

// Forward declarations to satisfy the methods in ComponentType
class ComponentTypeDef;
class ComponentType;
struct ComponentRecordField;
struct ComponentVariantCase;
struct ComponentResultType;
struct ComponentFuncType;
class ComponentResourceType;
using ComponentTypeVector =
    mozilla::Vector<ComponentType, 0, SystemAllocPolicy>;
using ComponentRecordFieldVector =
    mozilla::Vector<ComponentRecordField, 0, SystemAllocPolicy>;
using ComponentVariantCaseVector =
    mozilla::Vector<ComponentVariantCase, 0, SystemAllocPolicy>;

// The type of an item within a component.
class ComponentType {
  // TODO(wasm-cm): See if we could do a fancy tagging scheme to store the kind
  // in the bits of the pointer. It's a bit funky because right now we use high
  // bits in the kind for various purposes and so we can't pack it down into 3
  // or 4 bits like you'd want.
  ComponentTypeKind kind_;

  RefPtr<ComponentTypeDef> typeDef_;

  explicit ComponentType(ComponentTypeKind kind)
      : kind_(kind), typeDef_(nullptr) {
    MOZ_ASSERT(ComponentTypeKindIsPrimitive(kind));
  }
  explicit ComponentType(ComponentTypeKind kind,
                         RefPtr<ComponentTypeDef> typeDef)
      : kind_(kind), typeDef_(std::move(typeDef)) {}

 public:
  ComponentType() : kind_(ComponentTypeKind(0)), typeDef_(nullptr) {}
  bool isValid() const { return kind_ != ComponentTypeKind(0); }

  static ComponentType primitive(ComponentTypeKind kind) {
    MOZ_RELEASE_ASSERT(ComponentTypeKindIsPrimitive(kind));
    return ComponentType(kind);
  }
  static ComponentType record(ComponentRecordFieldVector&& fields);
  static ComponentType variant(ComponentVariantCaseVector&& cases);
  static ComponentType list(ComponentType&& elemType);
  static ComponentType tuple(ComponentTypeVector&& items);
  static ComponentType flags(ComponentStringVector&& labels);
  static ComponentType enum_(ComponentStringVector&& cases);
  static ComponentType option(ComponentType&& type);
  static ComponentType result(ComponentResultType&& type);
  static ComponentType own(ComponentType&& type);
  static ComponentType borrow(ComponentType&& type);
  static ComponentType func(ComponentFuncType&& type);
  static ComponentType resource(ComponentResourceType&& type);
  static ComponentType subResource();

  ComponentTypeKind kind() const { return kind_; }
  RefPtr<ComponentTypeDef> typeDef() const { return typeDef_; }

  const ComponentRecordFieldVector& asRecord() const;
  const ComponentVariantCaseVector& asVariant() const;
  ComponentType asList() const;
  const ComponentTypeVector& asTuple() const;
  const ComponentStringVector& asFlags() const;
  const ComponentStringVector& asEnum() const;
  ComponentType asOption() const;
  ComponentResultType asResult() const;
  ComponentType asOwn() const;
  ComponentType asBorrow() const;
  const ComponentFuncType& asFunc() const;
  const ComponentResourceType& asResource() const;

  bool operator==(const ComponentType& other) const {
    return kind_ == other.kind_ && typeDef_ == other.typeDef_;
  }
  static bool maybeEquals(mozilla::Maybe<ComponentType> a,
                          mozilla::Maybe<ComponentType> b) {
    if (a.isNothing() && b.isNothing()) {
      return true;
    }
    if (a.isSome() != b.isSome()) {
      return false;
    }
    return *a == *b;
  }
};

static_assert(std::is_default_constructible_v<ComponentType>);
static_assert(std::is_copy_constructible_v<ComponentType>);

struct ComponentTypeHasher {
  using Key = ComponentType;
  using Lookup = ComponentType;

  static HashNumber hash(const Lookup& aLookup);
  static bool match(const Key& aKey, const Lookup& aLookup);
};

struct ComponentCanonicalTypeSet {
  mozilla::HashSet<ComponentType, ComponentTypeHasher, SystemAllocPolicy>
      canonicalTypes_;

  bool canonicalize(const ComponentType& type, ComponentType* canonicalized);
};

// Canonicalizes `type` against the process-wide canonical type set, returning
// the canonical representative through `*canonicalized`. Thread-safe.
[[nodiscard]] bool CanonicalizeComponentType(const ComponentType& type,
                                             ComponentType* canonicalized);

// Empties the process-wide canonical type set. Intended for shutdown / testing.
void PurgeComponentCanonicalTypes();

struct ComponentRecordField {
  ComponentString name;
  ComponentType type;

  ComponentRecordField(ComponentString name_, ComponentType type_)
      : name(name_), type(type_) {}

  bool operator==(const ComponentRecordField& other) const {
    return name == other.name && type == other.type;
  }
};

struct ComponentVariantCase {
  ComponentString name;
  mozilla::Maybe<ComponentType> type;

  bool operator==(const ComponentVariantCase& other) const {
    return name == other.name && ComponentType::maybeEquals(type, other.type);
  }
};

struct ComponentResultType {
  mozilla::Maybe<ComponentType> type;
  mozilla::Maybe<ComponentType> errorType;

  static bool equals(const ComponentResultType& a,
                     const ComponentResultType& b) {
    return ComponentType::maybeEquals(a.type, b.type) &&
           ComponentType::maybeEquals(a.errorType, b.errorType);
  }
};

struct ComponentFuncType {
  ComponentTypeVector paramTypes;
  ComponentStringVector paramNames;
  mozilla::Maybe<ComponentType> resultType;

  bool operator==(const ComponentFuncType& other) const {
    MOZ_RELEASE_ASSERT(paramTypes.length() == paramNames.length());
    MOZ_RELEASE_ASSERT(other.paramTypes.length() == other.paramNames.length());
    if (paramTypes.length() != other.paramTypes.length()) {
      return false;
    }

    for (size_t i = 0; i < paramTypes.length(); i++) {
      if (paramTypes[i] != other.paramTypes[i] ||
          paramNames[i] != other.paramNames[i]) {
        return false;
      }
    }

    if (!ComponentType::maybeEquals(resultType, other.resultType)) {
      return false;
    }

    return true;
  }
};

class ComponentResourceType {
  // All resource types have (rep i32) for the time being.

  // Every resource type can be uniquely identified by this identifier
  // (including (sub resource) bounds, but that is stored elsewhere).
  uint32_t resourceID_;

  // If negative, no dtor.
  int32_t dtorIndex_;

 public:
  explicit ComponentResourceType(uint32_t id, int32_t dtorIndex = -1)
      : resourceID_(id), dtorIndex_(dtorIndex) {}

  uint32_t id() const { return resourceID_; }

  bool hasDtor() const { return dtorIndex_ >= 0; }
  uint32_t dtorIndex() const {
    MOZ_RELEASE_ASSERT(hasDtor());
    return dtorIndex_;
  }
};

inline uint32_t NewResourceId() {
  static mozilla::Atomic<uint32_t, mozilla::Relaxed> counter{0};
  return ++counter;
}

using ComponentTypeSchema = mozilla::Variant<
    mozilla::Nothing, ComponentType, ComponentRecordFieldVector,
    ComponentVariantCaseVector, ComponentTypeVector, ComponentStringVector,
    ComponentResultType, ComponentFuncType, ComponentResourceType>;

class ComponentTypeDef : public AtomicRefCounted<ComponentTypeDef> {
  ComponentTypeSchema schema_;

 public:
  explicit ComponentTypeDef(ComponentTypeSchema&& schema)
      : schema_(std::move(schema)) {}

  const ComponentTypeSchema& schema() const { return schema_; }
};

class Component;

[[nodiscard]] bool FlattenTypes(const Component& c,
                                const ComponentTypeVector& types,
                                ValTypeVector* result);
[[nodiscard]] bool FlattenType(const Component& c, const ComponentType& type,
                               ValTypeVector* result);
[[nodiscard]] bool FlattenRecord(const Component& c,
                                 const ComponentRecordFieldVector& fields,
                                 ValTypeVector* result);
mozilla::Maybe<FuncType> FlattenFuncType(const Component& c,
                                         const ComponentFuncType& funcType);

// A hash policy for StronglyUniqueNameSet that hashes items based on their
// trimmed, lowercased versions, but matches based on the full strongly-unique
// rules.
//
// The full strongly-unique rules are not hash-friendly; we have not yet figured
// out any way to "normalize" the name to a unique key that satisfies the
// strange carve-out rules for constructor and method names. But, we don't want
// to quadratically check each new name against every other name, so we take a
// disappointing halfway approach of hashing only the base part of the name, and
// then running the full strongly-unique logic in `match`. This results in more
// hash collisions and a less-inexpensive `match` method, but at least it keeps
// things from growing quadratically.
struct StronglyUniqueNameHasher {
  using Key = mozilla::Span<const char>;
  using Lookup = mozilla::Span<const char>;

  static HashNumber hash(const Lookup& aLookup);
  static bool match(const Key& aKey, const Lookup& aLookup);
};

// A class which can be used to check if a set of component model names is
// strongly-unique.
class StronglyUniqueNameSet {
  mozilla::HashSet<ComponentString, StronglyUniqueNameHasher, SystemAllocPolicy>
      data_;

 public:
  [[nodiscard]] bool add(ComponentString name, bool* duplicate);
};

struct ComponentCanonOpt {
  // TODO(wasm-cm)
};

using ComponentCanonOptVector =
    mozilla::Vector<ComponentCanonOpt, 0, SystemAllocPolicy>;

class ComponentFuncDesc {
  uint32_t typeIndex_;
  ComponentCanonOptVector canonOpts_;

 public:
  ComponentFuncDesc(uint32_t typeIndex, ComponentCanonOptVector&& canonOpts)
      : typeIndex_(typeIndex), canonOpts_(std::move(canonOpts)) {}

  // This returns the raw type index. To get the ComponentFuncType, call
  // Component::typeForFunc instead.
  uint32_t typeIndex() const { return typeIndex_; }
  const ComponentCanonOptVector& canonOpts() const { return canonOpts_; }
};

enum class ComponentAliasKind : uint8_t {
  CoreExport,
  Export,
  Outer,
};

// A generalized "alias" to an item in the component model. In addition to true
// aliases, a ComponentAlias may also reference an import, an export, or an item
// defined in the component itself. This is the main type used for each index
// space in the component model, as imports, exports, aliases, and defined items
// can be interleaved in any order.
//
// The data is stored into two fields, one of which identifies the index space
// for the item (possibly in another component), and the other of which is the
// index in that index space.
//
// This first field stores all the information necessary to find the index space
// for the item. It is a packed field laid out like so:
//
//     00 00 00000000 00000000000000000000
//     │  │  │        └ instance index (Kind::Alias only)
//     │  │  └ alias sort (type ComponentSort, Kind::Alias only)
//     │  └ alias kind (type AliasKind, Kind::Alias only)
//     └ kind (type Kind)
//
// For all kinds except Kind::Alias, this is basically a big 32-bit enum where
// only the top two bits are used. But for Kind::Alias we additionally store the
// component-level AliasKind (core export alias, component export alias, or
// outer alias) and the ComponentSort (e.g. Func or Type). Finally there is the
// instance index, which is the index of the core instance, component instance,
// or outer component to fetch an item from.
//
// The second field is simply a uint32_t item index like you'd find anywhere
// else. Together, this means the common case for defined items, imports, and
// exports is just:
//
//     if (firstField == (Kind::Defined << KindShift)) {
//         return items[secondField];
//     }
//
class ComponentAlias {
  uint32_t whatAndWhere_;
  uint32_t itemIndex_;

 public:
  static constexpr uint32_t KindShift = 30;
  static constexpr uint32_t KindMask = 0b11 << KindShift;
  static constexpr uint32_t AliasKindShift = 28;
  static constexpr uint32_t AliasKindMask = 0b11 << AliasKindShift;
  static constexpr uint32_t AliasSortShift = 20;
  static constexpr uint32_t AliasSortMask = 0b11111111 << AliasSortShift;
  static constexpr uint32_t AliasInstanceMask = (1 << AliasSortShift) - 1;

  enum class Kind : uint8_t {
    Defined,
    Import,
    Export,
    Alias,
  };

  explicit ComponentAlias(Kind kind, uint32_t itemIndex)
      : whatAndWhere_(uint32_t(kind) << KindShift), itemIndex_(itemIndex) {
    MOZ_ASSERT(kind != Kind::Alias);
    MOZ_ASSERT(this->kind() == kind);
  }
  explicit ComponentAlias(ComponentAliasKind aliasKind, ComponentSort sort,
                          uint32_t instanceIndex, uint32_t itemIndex)
      : whatAndWhere_(0), itemIndex_(itemIndex) {
    MOZ_ASSERT((instanceIndex & ~AliasInstanceMask) == 0);
    whatAndWhere_ |= uint32_t(Kind::Alias) << KindShift;
    whatAndWhere_ |= uint32_t(aliasKind) << AliasKindShift;
    whatAndWhere_ |= uint32_t(sort) << AliasSortShift;
    whatAndWhere_ |= instanceIndex;

    MOZ_ASSERT(kind() == Kind::Alias);
    MOZ_ASSERT(this->aliasKind() == aliasKind);
    MOZ_ASSERT(aliasSort() == sort);
    MOZ_ASSERT(aliasInstanceIndex() == instanceIndex);
  }

 public:
  static ComponentAlias defined(uint32_t itemIndex) {
    return ComponentAlias(Kind::Defined, itemIndex);
  }
  static ComponentAlias import(uint32_t itemIndex) {
    return ComponentAlias(Kind::Import, itemIndex);
  }
  static ComponentAlias export_(uint32_t itemIndex) {
    return ComponentAlias(Kind::Export, itemIndex);
  }
  static ComponentAlias alias(ComponentAliasKind aliasKind, ComponentSort sort,
                              uint32_t instanceIndex, uint32_t itemIndex) {
    return ComponentAlias(aliasKind, sort, instanceIndex, itemIndex);
  }

  Kind kind() const { return Kind((whatAndWhere_ & KindMask) >> KindShift); }
  bool isDefined() const {
    MOZ_ASSERT((whatAndWhere_ & ~KindMask) == 0);
    return whatAndWhere_ == (uint32_t(Kind::Defined) << KindShift);
  }
  bool isImport() const {
    MOZ_ASSERT((whatAndWhere_ & ~KindMask) == 0);
    return whatAndWhere_ == (uint32_t(Kind::Import) << KindShift);
  }
  bool isExport() const {
    MOZ_ASSERT((whatAndWhere_ & ~KindMask) == 0);
    return whatAndWhere_ == (uint32_t(Kind::Export) << KindShift);
  }
  bool isAlias() const {
    return (whatAndWhere_ & KindMask) == (uint32_t(Kind::Alias) << KindShift);
  }

  uint32_t itemIndex() const { return itemIndex_; }

  ComponentAliasKind aliasKind() const {
    MOZ_RELEASE_ASSERT(isAlias());
    return ComponentAliasKind((whatAndWhere_ & AliasKindMask) >>
                              AliasKindShift);
  }
  ComponentSort aliasSort() const {
    MOZ_RELEASE_ASSERT(isAlias());
    return ComponentSort((whatAndWhere_ & AliasSortMask) >> AliasSortShift);
  }
  uint32_t aliasInstanceIndex() const {
    MOZ_RELEASE_ASSERT(isAlias());
    return whatAndWhere_ & AliasInstanceMask;
  }
};

// TODO(wasm-cm): Add static asserts for MaxComponents and
// MaxComponentNestingDepth or whatever, eventually
static_assert(MaxComponentCoreInstances <= ComponentAlias::AliasInstanceMask);

struct CoreInstanceInstantiateArg {
  CacheableName name;
  uint32_t instanceIndex;
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
// TODO(wasm-cm): Fill this out and figure out how to satisfy the module's
// imports.
struct CoreInstanceDescFromInlineExports {
  SharedModule mod;
};

// Instructions for instantiating a core instance.
using CoreInstanceDesc = mozilla::Variant<CoreInstanceDescFromModule,
                                          CoreInstanceDescFromInlineExports>;

// Describes an import or export from a wasm component.
class ComponentExternDesc {
  ComponentSort sort_;
  ComponentType type_;

  // TODO(wasm-cm): This is a total hack, but since we currently don't have a
  // notion of core module types, we actually just store the index of the
  // relevant core module within the component. This obviously will not work as
  // soon as we do anything with multiple components.
  uint32_t coreModuleIndex_;

  explicit ComponentExternDesc(ComponentSort sort, ComponentType&& type)
      : sort_(sort), type_(std::move(type)) {
    MOZ_ASSERT(ComponentSortValidForExternDesc(sort));
  }
  explicit ComponentExternDesc(uint32_t coreModuleIndex)
      : sort_(ComponentSort::CoreModule), coreModuleIndex_(coreModuleIndex) {}

 public:
  ComponentExternDesc() = default;

  static ComponentExternDesc func(ComponentType&& funcType) {
    MOZ_ASSERT(funcType.kind() == ComponentTypeKind::Func);
    return ComponentExternDesc(ComponentSort::Func, std::move(funcType));
  }
  static ComponentExternDesc type(ComponentType&& type) {
    return ComponentExternDesc(ComponentSort::Type, std::move(type));
  }
  static ComponentExternDesc coreModule(uint32_t coreModuleIndex) {
    return ComponentExternDesc(coreModuleIndex);
  }

  bool isValid() const { return sort_ != ComponentSort::Invalid; }
  ComponentSort sort() const { return sort_; }
  ComponentType asFunc() const {
    MOZ_RELEASE_ASSERT(sort() == ComponentSort::Func);
    return type_;
  }
  ComponentType asType() const {
    MOZ_RELEASE_ASSERT(sort() == ComponentSort::Type);
    return type_;
  }
  uint32_t asCoreModule() const {
    MOZ_RELEASE_ASSERT(sort() == ComponentSort::CoreModule);
    // TODO(wasm-cm): This should obviously return a proper core module type,
    // when we actually support that.
    return coreModuleIndex_;
  }

  static bool matches(const ComponentExternDesc& sub,
                      const ComponentExternDesc& super);
};

static_assert(std::is_default_constructible_v<ComponentExternDesc>);

class ComponentImport {
  ComponentString name_;
  ComponentExternDesc externDesc_;

 public:
  explicit ComponentImport(ComponentString name,
                           const ComponentExternDesc& externDesc)
      : name_(name), externDesc_(externDesc) {}

  const ComponentString& name() const { return name_; }
  const ComponentExternDesc& externDesc() const { return externDesc_; }
};

class ComponentExport {
  ComponentString name_;
  ComponentExternDesc externDesc_;

 public:
  explicit ComponentExport(ComponentString name, ComponentExternDesc externDesc)
      : name_(name), externDesc_(externDesc) {}

  const ComponentString& name() const { return name_; }
  const ComponentExternDesc& externDesc() const { return externDesc_; }
};

// TODO(wasm-cm): This type is enormous, but a lot of the storage is due to
// containers like HashMap and Vector that aren't actually required once the
// component is built and validated. It would probably be smart to split this
// into ComponentBuilder and Component classes so that the final version can be
// smaller. (After all, we will have a lot of components in practice!)
class Component : public JS::WasmComponent {
 public:
  using CoreModuleVector = mozilla::Vector<SharedModule, 0, SystemAllocPolicy>;
  using CoreInstanceVector =
      mozilla::Vector<CoreInstanceDesc, 0, SystemAllocPolicy>;
  using TypeVector = mozilla::Vector<ComponentType, 0, SystemAllocPolicy>;
  using FuncVector = mozilla::Vector<ComponentFuncDesc, 0, SystemAllocPolicy>;
  using ImportVector = mozilla::Vector<ComponentImport, 0, SystemAllocPolicy>;
  using ExportVector = mozilla::Vector<ComponentExport, 0, SystemAllocPolicy>;
  using AliasVector = mozilla::Vector<ComponentAlias, 0, SystemAllocPolicy>;

 private:
  CoreModuleVector definedCoreModules_;
  CoreInstanceVector definedCoreInstances_;
  TypeVector definedTypes_;
  FuncVector definedFuncs_;
  ImportVector imports_;
  ExportVector exports_;

  AliasVector funcs_;
  AliasVector types_;
  AliasVector components_;
  AliasVector instances_;
  AliasVector coreFuncs_;
  AliasVector coreTables_;
  AliasVector coreMemories_;
  AliasVector coreGlobals_;
  AliasVector coreTags_;
  AliasVector coreTypes_;
  AliasVector coreModules_;
  AliasVector coreInstances_;

  LifoAlloc stringStorage_;
  mozilla::HashSet<ComponentString, NameHasher, SystemAllocPolicy>
      stringInterner_;

  template <typename T>
  bool addDefinedItem(T&& item,
                      mozilla::Vector<T, 0, SystemAllocPolicy>& itemsVector,
                      AliasVector& aliasVector) {
    uint32_t index = itemsVector.length();
    if (!itemsVector.append(std::forward<T>(item))) {
      return false;
    }
    return aliasVector.append(ComponentAlias::defined(index));
  }

 public:
  Component() : stringStorage_(1024, js::MallocArena) {}

  [[nodiscard]] bool internString(mozilla::Span<const char> str,
                                  ComponentString* out);

  // --------------------------------------------------------------------------
  // Accessors and adders for each index space

  const ImportVector& imports() const { return imports_; }
  [[nodiscard]] bool addImport(ComponentImport&& import,
                               StronglyUniqueNameSet& nameDedup,
                               bool* duplicate);

  const ExportVector& exports() const { return exports_; }
  [[nodiscard]] bool addExport(ComponentExport&& exp,
                               StronglyUniqueNameSet& nameDedup,
                               bool* duplicate);

  const AliasVector& funcs() const { return funcs_; }
  [[nodiscard]] bool addFunc(ComponentFuncDesc&& func) {
    return addDefinedItem(std::move(func), definedFuncs_, funcs_);
  }

  const AliasVector& types() const { return types_; }
  [[nodiscard]] bool addType(ComponentType&& type) {
    MOZ_RELEASE_ASSERT(type.isValid());
    return addDefinedItem(std::move(type), definedTypes_, types_);
  }

  // TODO(wasm-cm): Functions for components
  // TODO(wasm-cm): Functions for component instances

  const AliasVector& coreFuncs() const { return coreFuncs_; }
  [[nodiscard]] bool addCoreFunc(ComponentAlias&& funcAlias) {
    return coreFuncs_.append(std::move(funcAlias));
  }

  const AliasVector& coreTables() const { return coreTables_; }
  [[nodiscard]] bool addCoreTable(ComponentAlias&& tableAlias) {
    return coreTables_.append(std::move(tableAlias));
  }

  const AliasVector& coreMemories() const { return coreMemories_; }
  [[nodiscard]] bool addCoreMemory(ComponentAlias&& memoryAlias) {
    return coreMemories_.append(std::move(memoryAlias));
  }

  const AliasVector& coreGlobals() const { return coreGlobals_; }
  [[nodiscard]] bool addCoreGlobal(ComponentAlias&& globalAlias) {
    return coreGlobals_.append(std::move(globalAlias));
  }

  const AliasVector& coreTags() const { return coreTags_; }
  bool addCoreTag(ComponentAlias&& tagAlias) {
    return coreTags_.append(std::move(tagAlias));
  }

  const AliasVector& coreModules() const { return coreModules_; }
  [[nodiscard]] bool addCoreModule(SharedModule module) {
    return addDefinedItem(std::move(module), definedCoreModules_, coreModules_);
  }

  const AliasVector& coreInstances() const { return coreInstances_; }
  [[nodiscard]] bool addCoreInstance(CoreInstanceDesc&& instance) {
    return addDefinedItem(std::move(instance), definedCoreInstances_,
                          coreInstances_);
  }

  // --------------------------------------------------------------------------
  // Utilities for accessing type information

  // Gets a type from the component's type index space. In many cases this may
  // be a type bound, which is typically irrelevant to the task at hand, so
  // consider calling `getResolvedType()` instead.
  ComponentType getType(uint32_t typeIndex) const {
    ComponentAlias alias = types_[typeIndex];
    switch (alias.kind()) {
      case ComponentAlias::Kind::Defined:
        return definedTypes_[alias.itemIndex()];
      case ComponentAlias::Kind::Import:
        return imports_[alias.itemIndex()].externDesc().asType();
      case ComponentAlias::Kind::Export:
        return exports_[alias.itemIndex()].externDesc().asType();
      case ComponentAlias::Kind::Alias:
        MOZ_CRASH("should be impossible for now");
      default:
        MOZ_CRASH();
    }
  }

  // Gets the type of a component func (not a core func). It is always safe to
  // call `.asFunc()` on the result.
  ComponentType getTypeForFunc(uint32_t funcIndex) const {
    ComponentAlias alias = funcs_[funcIndex];
    switch (alias.kind()) {
      case ComponentAlias::Kind::Defined:
        return getType(definedFuncs_[alias.itemIndex()].typeIndex());
      case ComponentAlias::Kind::Import:
        return imports_[alias.itemIndex()].externDesc().asFunc();
      case ComponentAlias::Kind::Export:
        return exports_[alias.itemIndex()].externDesc().asFunc();
      case ComponentAlias::Kind::Alias:
        MOZ_CRASH("should be impossible for now");
      default:
        MOZ_CRASH();
    }
  }

  // Gets the type of a core func (not a component func).
  const FuncType& getCoreFuncTypeForCoreFunc(uint32_t coreFuncIndex) const {
    ComponentAlias alias = coreFuncs_[coreFuncIndex];
    switch (alias.kind()) {
      case ComponentAlias::Kind::Defined: {
        // TODO(wasm-cm): Fix this when (canon lower) is supported.
        MOZ_CRASH("should be impossible for now");
      } break;
      case ComponentAlias::Kind::Import:
      case ComponentAlias::Kind::Export:
        // Core funcs cannot be imported or exported
        MOZ_CRASH();
      case ComponentAlias::Kind::Alias: {
        MOZ_ASSERT(alias.aliasKind() == ComponentAliasKind::CoreExport);
        SharedModule mod =
            getCoreModuleForCoreInstance(alias.aliasInstanceIndex());
        uint32_t ft = mod->codeMeta().funcs[alias.itemIndex()].typeIndex;
        return mod->codeMeta().types->type(ft).funcType();
      } break;
      default:
        MOZ_CRASH();
    }
  }

  SharedModule getCoreModule(uint32_t modIndex) const {
    ComponentAlias alias = coreModules_[modIndex];
    switch (alias.kind()) {
      case ComponentAlias::Kind::Defined:
        return definedCoreModules_[alias.itemIndex()];
      case ComponentAlias::Kind::Import:
        // TODO(wasm-cm): Fix when core module types are supported
        MOZ_CRASH("should be impossible for now");
      case ComponentAlias::Kind::Export: {
        const ComponentExport& exp = exports_[alias.itemIndex()];
        MOZ_ASSERT(exp.externDesc().sort() == ComponentSort::CoreModule);
        return definedCoreModules_[exp.externDesc().asCoreModule()];
      } break;
      case ComponentAlias::Kind::Alias:
        // TODO(wasm-cm): Fix when nested components are supported
        MOZ_CRASH("should be impossible for now");
      default:
        MOZ_CRASH();
    }
  }

  SharedModule getCoreModuleForCoreInstance(uint32_t instanceIndex) const {
    ComponentAlias alias = coreInstances_[instanceIndex];
    switch (alias.kind()) {
      case ComponentAlias::Kind::Defined: {
        const CoreInstanceDesc& instance =
            definedCoreInstances_[alias.itemIndex()];
        if (instance.is<CoreInstanceDescFromModule>()) {
          return getCoreModule(
              instance.as<CoreInstanceDescFromModule>().moduleIndex);
        }
        return instance.as<CoreInstanceDescFromInlineExports>().mod;
      } break;
      case ComponentAlias::Kind::Import:
      case ComponentAlias::Kind::Export:
        // Core instances cannot be imported or exported
        MOZ_CRASH();
      case ComponentAlias::Kind::Alias:
        // TODO(wasm-cm): Fix once nested components are supported
        MOZ_CRASH("should be impossible for now");
      default:
        MOZ_CRASH();
    }
  }

  size_t gcMallocBytesExcludingCode() const {
    // TODO(wasm-cm): Right now, this only sums up the sizes of the inner
    // modules, but this is not an accurate picture of a component's memory
    // footprint.
    size_t total = 0;
    for (const SharedModule& module : definedCoreModules_) {
      total += module->gcMallocBytesExcludingCode();
    }
    return total;
  }

  size_t tier1CodeMemoryUsed() const {
    // TODO(wasm-cm): As above, this only sums up the memory for core modules,
    // and does not account for other potential code memory.
    size_t total = 0;
    for (const SharedModule& module : definedCoreModules_) {
      total += module->tier1CodeMemoryUsed();
    }
    return total;
  }

 private:
  // JS API and JS::WasmComponent implementation:
  JSObject* createObject(JSContext* cx) const override;
};

using MutableComponent = RefPtr<Component>;
using SharedComponent = RefPtr<const Component>;

}  // namespace wasm
}  // namespace js

#endif  // ENABLE_WASM_COMPONENTS

#endif  // wasm_component_h
