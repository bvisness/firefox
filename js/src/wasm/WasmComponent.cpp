/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "wasm/WasmComponent.h"

#ifdef ENABLE_WASM_COMPONENTS

#  include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#  include "threading/ExclusiveData.h"
#  include "util/Text.h"
#  include "vm/GlobalObject.h"
#  include "vm/MutexIDs.h"
#  include "wasm/WasmJS.h"

using namespace js;
using namespace js::wasm;

static constexpr mozilla::Span<const char> attributeConstructor =
    mozilla::MakeStringSpan("[constructor]");
static constexpr mozilla::Span<const char> attributeMethod =
    mozilla::MakeStringSpan("[method]");
static constexpr mozilla::Span<const char> attributeStatic =
    mozilla::MakeStringSpan("[static]");

// Component model names are encoded as UTF-8, and in fact an ASCII subset of
// UTF-8, so this is fine.
static char LowercaseNameChar(char c) {
  return ('A' <= c && c <= 'Z') ? c + ('a' - 'A') : c;
}

static mozilla::Span<const char> TrimAttribute(mozilla::Span<const char> name) {
  if (CharsStartsWith(name, attributeConstructor)) {
    return name.Subspan(attributeConstructor.Length());
  }
  if (CharsStartsWith(name, attributeMethod)) {
    return name.Subspan(attributeMethod.Length());
  }
  if (CharsStartsWith(name, attributeStatic)) {
    return name.Subspan(attributeStatic.Length());
  }
  return name;
}

static bool NameHasAttribute(mozilla::Span<const char> name) {
  // The name should already be well-formed from parse time.
  return name.Length() == 0 || name.data()[0] == '[';
}

static HashNumber AddComponentStringToHash(HashNumber hash,
                                           const ComponentString& s) {
  for (char c : s) {
    hash = mozilla::AddToHash(hash, c);
  }
  return hash;
}

// We hash only the base part of the name, e.g. "foo" for "[constructor]foo".
HashNumber StronglyUniqueNameHasher::hash(const Lookup& aLookup) {
  const Lookup& trimmed = TrimAttribute(aLookup);

  HashNumber hash = 0;
  for (size_t i = 0; i < trimmed.Length(); i++) {
    char c = trimmed.data()[i];
    if (c == '.') {
      break;
    }
    hash = mozilla::AddToHash(hash, LowercaseNameChar(trimmed.data()[i]));
  }
  return hash;
}

bool StronglyUniqueNameHasher::match(const Key& aKey, const Lookup& aLookup) {
  mozilla::Span<const char> newTrimmed = TrimAttribute(aLookup);
  mozilla::Span<const char> existingTrimmed = TrimAttribute(aKey);

  // Rule 1: If one name is l and the other name is [constructor]l (for the
  // same label l), they are strongly-unique.
  bool newIsConstructor = CharsStartsWith(aLookup, attributeConstructor);
  bool existingIsConstructor = CharsStartsWith(aKey, attributeConstructor);
  if (newIsConstructor != existingIsConstructor &&
      newTrimmed == existingTrimmed) {
    return false;
  }

  // Rule 2: If one name is l and the other name is [*]l.l (for the same label l
  // and any annotation * with a dotted l.l name), they are not strongly-unique.
  mozilla::Maybe<mozilla::Span<const char>> plain;
  mozilla::Maybe<mozilla::Span<const char>> dotted;
  if (!NameHasAttribute(aLookup)) {
    plain.emplace(aLookup);
  } else if (!NameHasAttribute(aKey)) {
    plain.emplace(aKey);
  }
  if (CharsStartsWith(aLookup, attributeMethod) ||
      CharsStartsWith(aLookup, attributeStatic)) {
    dotted.emplace(aLookup);
  } else if (CharsStartsWith(aKey, attributeMethod) ||
             CharsStartsWith(aKey, attributeStatic)) {
    dotted.emplace(aKey);
  }
  if (plain.isSome() && dotted.isSome()) {
    mozilla::Span<const char> dottedTrimmed = TrimAttribute(dotted.value());
    size_t indexOfDot = dottedTrimmed.IndexOf('.');
    MOZ_RELEASE_ASSERT(indexOfDot != mozilla::Span<const char>::npos);
    auto [before, after] = dottedTrimmed.SplitAt(indexOfDot);
    after = after.Subspan(1);  // The SplitAt method includes the dot.
    if (plain.value() == after && plain.value() == before) {
      return true;
    }
  }

  // Rule 3: Lowercase the names, trim attributes, and compare directly.
  if (newTrimmed.Length() != existingTrimmed.Length()) {
    return false;
  }
  for (size_t i = 0; i < newTrimmed.Length(); i++) {
    if (LowercaseNameChar(newTrimmed[i]) !=
        LowercaseNameChar(existingTrimmed[i])) {
      return false;
    }
  }
  return true;
}

bool StronglyUniqueNameSet::add(ComponentString name, bool* duplicate) {
  *duplicate = false;

  auto p = data_.lookupForAdd(name);
  if (p) {
    *duplicate = true;
    return true;
  }

  return data_.add(p, std::move(name));
}

bool ComponentExternDesc::matches(const ComponentExternDesc& sub,
                                  const ComponentExternDesc& super) {
  MOZ_ASSERT(ComponentSortValidForExternDesc(sub.sort()));
  MOZ_ASSERT(ComponentSortValidForExternDesc(super.sort()));
  MOZ_RELEASE_ASSERT(sub.isValid() && super.isValid());

  // Different sorts never match.
  if (sub.sort() != super.sort()) {
    return false;
  }

  switch (sub.sort()) {
    case ComponentSort::Func:
      return sub.asFunc() == super.asFunc();
    case ComponentSort::Type:
      return sub.asType() == super.asType();
    case ComponentSort::Component:
    case ComponentSort::Instance:
    case ComponentSort::CoreModule: {
      // TODO(wasm-cm)
      return false;
    } break;
    default:
      MOZ_CRASH("all valid sorts for externdesc should have been handled");
  }
}

ComponentType ComponentType::record(ComponentRecordFieldVector&& fields) {
  return ComponentType(
      ComponentTypeKind::Record,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(fields))));
}

ComponentType ComponentType::variant(ComponentVariantCaseVector&& cases) {
  return ComponentType(
      ComponentTypeKind::Variant,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(cases))));
}

ComponentType ComponentType::list(ComponentType&& elemType) {
  return ComponentType(
      ComponentTypeKind::List,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(elemType))));
}

ComponentType ComponentType::tuple(ComponentTypeVector&& items) {
  return ComponentType(
      ComponentTypeKind::Tuple,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(items))));
}

ComponentType ComponentType::flags(ComponentStringVector&& labels) {
  return ComponentType(
      ComponentTypeKind::Flags,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(labels))));
}

ComponentType ComponentType::enum_(ComponentStringVector&& cases) {
  return ComponentType(
      ComponentTypeKind::Enum,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(cases))));
}

ComponentType ComponentType::option(ComponentType&& type) {
  return ComponentType(
      ComponentTypeKind::Option,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(type))));
}

ComponentType ComponentType::result(ComponentResultType&& type) {
  return ComponentType(
      ComponentTypeKind::Result,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(type))));
}

ComponentType ComponentType::own(ComponentType&& type) {
  return ComponentType(
      ComponentTypeKind::Own,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(type))));
}

ComponentType ComponentType::borrow(ComponentType&& type) {
  return ComponentType(
      ComponentTypeKind::Borrow,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(type))));
}

ComponentType ComponentType::func(ComponentFuncType&& type) {
  return ComponentType(
      ComponentTypeKind::Func,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(type))));
}

ComponentType ComponentType::resource(ComponentResourceType&& type) {
  return ComponentType(
      ComponentTypeKind::Resource,
      js_new<ComponentTypeDef>(ComponentTypeSchema(std::move(type))));
}

ComponentType ComponentType::subResource() {
  // We still need a unique heap allocation so that two (sub resource) types
  // will not be equal.
  return ComponentType(
      ComponentTypeKind::SubResource,
      js_new<ComponentTypeDef>(ComponentTypeSchema(mozilla::Nothing())));
}

const ComponentRecordFieldVector& ComponentType::asRecord() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Record);
  return typeDef_->schema().as<ComponentRecordFieldVector>();
}

const ComponentVariantCaseVector& ComponentType::asVariant() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Variant);
  return typeDef_->schema().as<ComponentVariantCaseVector>();
}

ComponentType ComponentType::asList() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::List);
  return typeDef_->schema().as<ComponentType>();
}

const ComponentTypeVector& ComponentType::asTuple() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Tuple);
  return typeDef_->schema().as<ComponentTypeVector>();
}

const ComponentStringVector& ComponentType::asFlags() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Flags);
  return typeDef_->schema().as<ComponentStringVector>();
}

const ComponentStringVector& ComponentType::asEnum() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Enum);
  return typeDef_->schema().as<ComponentStringVector>();
}

ComponentType ComponentType::asOption() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Option);
  return typeDef_->schema().as<ComponentType>();
}

ComponentResultType ComponentType::asResult() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Result);
  return typeDef_->schema().as<ComponentResultType>();
}

ComponentType ComponentType::asOwn() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Own);
  return typeDef_->schema().as<ComponentType>();
}

ComponentType ComponentType::asBorrow() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Borrow);
  return typeDef_->schema().as<ComponentType>();
}

const ComponentFuncType& ComponentType::asFunc() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Func);
  return typeDef_->schema().as<ComponentFuncType>();
}

const ComponentResourceType& ComponentType::asResource() const {
  MOZ_RELEASE_ASSERT(kind() == ComponentTypeKind::Resource);
  return typeDef_->schema().as<ComponentResourceType>();
}

[[nodiscard]] static HashNumber AddComponentTypeToHash(HashNumber hash,
                                                       ComponentType type) {
  hash = mozilla::AddToHash(hash, type.kind());
  hash = mozilla::AddToHash(hash, type.typeDef().get());
  return hash;
}

[[nodiscard]] static HashNumber AddMaybeComponentTypeToHash(
    HashNumber hash, mozilla::Maybe<ComponentType> type) {
  hash = mozilla::AddToHash(hash, type.isSome());
  if (type.isSome()) {
    hash = AddComponentTypeToHash(hash, *type);
  }
  return hash;
}

HashNumber ComponentTypeHasher::hash(const ComponentType& t) {
  HashNumber hash = 0;
  hash = mozilla::AddToHash(hash, t.kind());

  // Primitives and resource types should not appear here; this is caught by the
  // default case.
  switch (t.kind()) {
    case ComponentTypeKind::Record: {
      const ComponentRecordFieldVector& fields = t.asRecord();
      for (const ComponentRecordField& f : fields) {
        hash = AddComponentStringToHash(hash, f.name);
        hash = AddComponentTypeToHash(hash, f.type);
      }
    } break;
    case ComponentTypeKind::Variant: {
      const ComponentVariantCaseVector& cases = t.asVariant();
      for (const ComponentVariantCase& c : cases) {
        hash = AddComponentStringToHash(hash, c.name);
        hash = AddMaybeComponentTypeToHash(hash, c.type);
      }
    } break;
    case ComponentTypeKind::List: {
      hash = AddComponentTypeToHash(hash, t.asList());
    } break;
    case ComponentTypeKind::Tuple: {
      const ComponentTypeVector& types = t.asTuple();
      for (ComponentType t : types) {
        hash = AddComponentTypeToHash(hash, t);
      }
    } break;
    case ComponentTypeKind::Flags: {
      const ComponentStringVector& labels = t.asFlags();
      for (ComponentString label : labels) {
        hash = AddComponentStringToHash(hash, label);
      }
    } break;
    case ComponentTypeKind::Enum: {
      const ComponentStringVector& cases = t.asEnum();
      for (ComponentString c : cases) {
        hash = AddComponentStringToHash(hash, c);
      }
    } break;
    case ComponentTypeKind::Option: {
      hash = AddComponentTypeToHash(hash, t.asOption());
    } break;
    case ComponentTypeKind::Result: {
      const ComponentResultType& rt = t.asResult();
      hash = AddMaybeComponentTypeToHash(hash, rt.type);
      hash = AddMaybeComponentTypeToHash(hash, rt.errorType);
    } break;
    case ComponentTypeKind::Own: {
      hash = AddComponentTypeToHash(hash, t.asOwn());
    } break;
    case ComponentTypeKind::Borrow: {
      hash = AddComponentTypeToHash(hash, t.asBorrow());
    } break;
    case ComponentTypeKind::Func: {
      const ComponentFuncType& ft = t.asFunc();
      MOZ_ASSERT(ft.paramTypes.length() == ft.paramNames.length());
      for (size_t i = 0; i < ft.paramTypes.length(); i++) {
        hash = AddComponentStringToHash(hash, ft.paramNames[i]);
        hash = AddComponentTypeToHash(hash, ft.paramTypes[i]);
      }
      hash = AddMaybeComponentTypeToHash(hash, ft.resultType);
    } break;
    case ComponentTypeKind::Component:
    case ComponentTypeKind::Instance:
      // TODO(wasm-cm): Component and instance types not yet implemented
      MOZ_CRASH();
    default:
      MOZ_CRASH("should have been excluded from hashing");
  }

  return hash;
}
bool ComponentTypeHasher::match(const ComponentType& a,
                                const ComponentType& b) {
  // (eq i) bounds should be resolved to a unique type on type construction.
  MOZ_ASSERT(a.kind() != ComponentTypeKind::Eq);
  MOZ_ASSERT(b.kind() != ComponentTypeKind::Eq);

  // Primitives and resource types should be special-cased during
  // canonicalization and should therefore never end up here.
  MOZ_ASSERT(!ComponentTypeKindIsPrimitive(a.kind()) &&
             a.kind() != ComponentTypeKind::Resource &&
             a.kind() != ComponentTypeKind::SubResource);
  MOZ_ASSERT(!ComponentTypeKindIsPrimitive(b.kind()) &&
             b.kind() != ComponentTypeKind::Resource &&
             b.kind() != ComponentTypeKind::SubResource);

  if (a.kind() != b.kind()) {
    return false;
  }
  switch (a.kind()) {
    case ComponentTypeKind::Record: {
      const ComponentRecordFieldVector& aFields = a.asRecord();
      const ComponentRecordFieldVector& bFields = b.asRecord();
      if (aFields.length() != bFields.length()) {
        return false;
      }
      for (size_t i = 0; i < aFields.length(); i++) {
        if (aFields[i] != bFields[i]) {
          return false;
        }
      }
      return true;
    } break;
    case ComponentTypeKind::Variant: {
      const ComponentVariantCaseVector& aCases = a.asVariant();
      const ComponentVariantCaseVector& bCases = b.asVariant();
      if (aCases.length() != bCases.length()) {
        return false;
      }
      for (size_t i = 0; i < aCases.length(); i++) {
        if (aCases[i] != bCases[i]) {
          return false;
        }
      }
      return true;
    } break;
    case ComponentTypeKind::List:
      return a.asList() == b.asList();
    case ComponentTypeKind::Tuple: {
      const ComponentTypeVector& aTypes = a.asTuple();
      const ComponentTypeVector& bTypes = b.asTuple();
      if (aTypes.length() != bTypes.length()) {
        return false;
      }
      for (size_t i = 0; i < aTypes.length(); i++) {
        if (aTypes[i] != bTypes[i]) {
          return false;
        }
      }
      return true;
    } break;
    case ComponentTypeKind::Flags: {
      const ComponentStringVector& aLabels = a.asFlags();
      const ComponentStringVector& bLabels = b.asFlags();
      if (aLabels.length() != bLabels.length()) {
        return false;
      }
      for (size_t i = 0; i < aLabels.length(); i++) {
        if (aLabels[i] != bLabels[i]) {
          return false;
        }
      }
      return true;
    } break;
    case ComponentTypeKind::Enum: {
      const ComponentStringVector& aLabels = a.asEnum();
      const ComponentStringVector& bLabels = b.asEnum();
      if (aLabels.length() != bLabels.length()) {
        return false;
      }
      for (size_t i = 0; i < aLabels.length(); i++) {
        if (aLabels[i] != bLabels[i]) {
          return false;
        }
      }
      return true;
    } break;
    case ComponentTypeKind::Option:
      return a.asOption() == b.asOption();
    case ComponentTypeKind::Result:
      return ComponentResultType::equals(a.asResult(), b.asResult());
    case ComponentTypeKind::Own:
      return a.asOwn() == b.asOwn();
    case ComponentTypeKind::Borrow:
      return a.asBorrow() == b.asBorrow();
    case ComponentTypeKind::Func:
      return a.asFunc() == b.asFunc();
    case ComponentTypeKind::Component:
    case ComponentTypeKind::Instance:
      // TODO(wasm-cm): Component and instance types are not yet implemented
      return false;
    default:
      MOZ_CRASH();
  }
}

bool ComponentCanonicalTypeSet::canonicalize(const ComponentType& type,
                                             ComponentType* canonicalized) {
  MOZ_RELEASE_ASSERT(type.isValid());

  if (ComponentTypeKindIsPrimitive(type.kind())) {
    MOZ_RELEASE_ASSERT(!type.typeDef());
    *canonicalized = type;
    return true;
  }
  MOZ_RELEASE_ASSERT(type.typeDef());

  if (type.kind() == ComponentTypeKind::Resource ||
      type.kind() == ComponentTypeKind::SubResource) {
    *canonicalized = type;
    return true;
  }

  auto addPtr = canonicalTypes_.lookupForAdd(type);
  if (addPtr) {
    *canonicalized = *addPtr;
    return true;
  }
  if (!canonicalTypes_.add(addPtr, type)) {
    return false;
  }
  *canonicalized = type;
  return true;
}

MOZ_RUNINIT static ExclusiveData<ComponentCanonicalTypeSet>
    sComponentCanonicalTypeSet(mutexid::WasmComponentCanonicalTypeSet);

bool wasm::CanonicalizeComponentType(const ComponentType& type,
                                     ComponentType* canonicalized) {
  ExclusiveData<ComponentCanonicalTypeSet>::Guard locked =
      sComponentCanonicalTypeSet.lock();
  return locked->canonicalize(type, canonicalized);
}

void wasm::PurgeComponentCanonicalTypes() {
  ExclusiveData<ComponentCanonicalTypeSet>::Guard locked =
      sComponentCanonicalTypeSet.lock();
  locked->canonicalTypes_.clearAndCompact();
}

mozilla::Maybe<FuncType> wasm::FlattenFuncType(
    const Component& c, const ComponentFuncType& funcType) {
  ValTypeVector params;
  ValTypeVector results;

  // TODO(wasm-cm): Handle (and test) the case where params or results exceed
  // the maximums set by the component model, at which point the ABI falls back
  // to passing values in memory. (Or maybe this will all change with lazy
  // lowering, who knows.)

  if (!FlattenTypes(c, funcType.paramTypes, &params)) {
    return mozilla::Nothing();
  }
  if (funcType.resultType.isSome()) {
    if (!FlattenType(c, funcType.resultType.ref(), &results)) {
      return mozilla::Nothing();
    }
  }

  return mozilla::Some(FuncType(std::move(params), std::move(results)));
}

bool wasm::FlattenTypes(const Component& c, const ComponentTypeVector& types,
                        ValTypeVector* result) {
  // Pre-reserve at least enough space for a bunch of primitives. We still may
  // exceed the capacity reserved here but at least we can avoid a little bit of
  // allocation. (Appends after this point are not to be considered infallible.)
  if (!result->reserve(types.length())) {
    return false;
  }

  for (const ComponentType& t : types) {
    if (!FlattenType(c, t, result)) {
      return false;
    }
  }

  return true;
}

static ValType JoinVariantValType(ValType a, ValType b) {
  MOZ_ASSERT(a.isNumber() && b.isNumber());
  if (a == b) {
    return a;
  } else if ((a == ValType::i32() && b == ValType::f32()) ||
             (a == ValType::f32() && b == ValType::i32())) {
    return ValType::i32();
  } else {
    return ValType::i64();
  }
}

bool wasm::FlattenType(const Component& c, const ComponentType& type,
                       ValTypeVector* result) {
  switch (type.kind()) {
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
    case ComponentTypeKind::Enum:
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
      if (!FlattenRecord(c, type.asRecord(), result)) {
        return false;
      }
    } break;
    case ComponentTypeKind::Tuple: {
      if (!FlattenTypes(c, type.asTuple(), result)) {
        return false;
      }
    } break;
    case ComponentTypeKind::Variant: {
      // Flatten the discriminant
      if (!result->append(ValType::i32())) {
        return false;
      }

      // Flatten all the cases (overlapped, with joins)
      const ComponentVariantCaseVector& cases = type.asVariant();
      size_t startIndex = result->length();
      for (const ComponentVariantCase& case_ : cases) {
        if (!case_.type) {
          continue;
        }

        ValTypeVector caseFlattened;
        if (!FlattenType(c, *case_.type, &caseFlattened)) {
          return false;
        }
        for (size_t i = 0; i < caseFlattened.length(); i++) {
          size_t existingIndex = startIndex + i;
          if (existingIndex < result->length()) {
            // Join the new type with the existing one.
            (*result)[existingIndex] =
                JoinVariantValType((*result)[existingIndex], caseFlattened[i]);
          } else {
            // Append the new type to the overall list.
            if (!result->append(caseFlattened[i])) {
              return false;
            }
          }
        }
      }
    } break;
    case ComponentTypeKind::Option: {
      ComponentType inner = type.asOption();
      if (!result->append(ValType::i32())) {
        return false;
      }
      if (!FlattenType(c, inner, result)) {
        return false;
      }
    } break;
    case ComponentTypeKind::Result: {
      ComponentResultType inner = type.asResult();
      // Result types are encoded just like a variant with two cases, but each
      // case may or may not have a type.

      // Discriminant
      if (!result->append(ValType::i32())) {
        return false;
      }

      // Payload(s)
      size_t startIndex = result->length();
      if (inner.type.isSome()) {
        if (!FlattenType(c, *inner.type, result)) {
          return false;
        }
      }
      if (inner.errorType.isSome()) {
        ValTypeVector errorFlattened;
        if (!FlattenType(c, *inner.errorType, &errorFlattened)) {
          return false;
        }
        for (size_t i = 0; i < errorFlattened.length(); i++) {
          size_t existingIndex = startIndex + i;
          if (existingIndex < result->length()) {
            (*result)[existingIndex] =
                JoinVariantValType((*result)[existingIndex], errorFlattened[i]);
          } else {
            if (!result->append(errorFlattened[i])) {
              return false;
            }
          }
        }
      }
    } break;

    default:
      MOZ_CRASH("should have been rejected when the func type was validated");
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

bool Component::internString(mozilla::Span<const char> str,
                             ComponentString* out) {
  MOZ_ASSERT(IsUtf8(str));

  // Return existing string if it was already interned
  auto p = stringInterner_.lookupForAdd(str);
  if (p) {
    *out = *p;
    return true;
  }

  // Store new string
  if (str.Length() == 0) {
    *out = ComponentString();
    return true;
  }
  char* buf = stringStorage_.newArrayUninitialized<char>(str.Length());
  if (!buf) {
    return false;
  }
  memcpy(buf, str.data(), str.Length());
  *out = ComponentString(buf, str.Length());

  // Add new string to the set
  if (!stringInterner_.add(p, *out)) {
    return false;
  }

  return true;
}

/* virtual */
JSObject* Component::createObject(JSContext* cx) const {
  if (!GlobalObject::ensureConstructor(cx, cx->global(), JSProto_WebAssembly)) {
    return nullptr;
  }

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

bool Component::addImport(ComponentImport&& import,
                          StronglyUniqueNameSet& nameDedup, bool* duplicate) {
  // Check name for duplicate
  if (!nameDedup.add(import.name(), duplicate) || *duplicate) {
    return false;
  }

  // Add import to imports vector
  uint32_t importIndex = imports_.length();
  if (!imports_.append(import)) {
    return false;
  }

  // Add import to appropriate index space
  ComponentAlias alias = ComponentAlias::import(importIndex);
  MOZ_ASSERT(ComponentSortValidForExternDesc(import.externDesc().sort()));
  switch (import.externDesc().sort()) {
    case ComponentSort::Func: {
      if (!funcs_.append(alias)) {
        return false;
      }
    } break;
    case ComponentSort::Type: {
      if (!types_.append(alias)) {
        return false;
      }
    } break;
    case ComponentSort::Component: {
      if (!components_.append(alias)) {
        return false;
      }
    } break;
    case ComponentSort::Instance: {
      if (!instances_.append(alias)) {
        return false;
      }
    } break;
    case ComponentSort::CoreModule: {
      if (!coreModules_.append(alias)) {
        return false;
      }
    } break;
    default:
      MOZ_CRASH();
  }

  return true;
}

bool Component::addExport(ComponentExport&& exp,
                          StronglyUniqueNameSet& nameDedup, bool* duplicate) {
  if (!nameDedup.add(exp.name(), duplicate) || *duplicate) {
    return false;
  }

  // Add export to exports vector
  uint32_t exportIndex = exports_.length();
  if (!exports_.append(std::move(exp))) {
    return false;
  }

  // Add export to appropriate index space
  ComponentAlias alias = ComponentAlias::export_(exportIndex);
  MOZ_ASSERT(ComponentSortValidForExternDesc(exp.externDesc().sort()));
  switch (exp.externDesc().sort()) {
    case ComponentSort::Func: {
      if (!funcs_.append(alias)) {
        return false;
      }
    } break;
    case ComponentSort::Type: {
      if (!types_.append(alias)) {
        return false;
      }
    } break;
    case ComponentSort::Component: {
      if (!components_.append(alias)) {
        return false;
      }
    } break;
    case ComponentSort::Instance: {
      if (!instances_.append(alias)) {
        return false;
      }
    } break;
    case ComponentSort::CoreModule: {
      if (!coreModules_.append(alias)) {
        return false;
      }
    } break;
    default:
      MOZ_CRASH();
  }

  return exports_.append(std::move(exp));
}

#endif  // ENABLE_WASM_COMPONENTS
