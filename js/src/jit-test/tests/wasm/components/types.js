// ----------------------------------------------------------------------------
// Primitive types
{
  const primitives = [
    "bool", "s8", "u8", "s16", "u16", "s32", "u32",
    "s64", "u64", "f32", "f64", "char", "string",
  ];
  for (const prim of primitives) {
    new WebAssembly.Component(wasmTextToBinary(`
      (component
        (type ${prim})
      )
    `));
  }
}

// ----------------------------------------------------------------------------
// Record types

// Basic record.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" u32) (field "y" u32)))
)
`));

// Empty record - should fail (spec requires at least one field).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record))
)
`)), WebAssembly.CompileError, /at least one field/);

// Record with type reference to a previously-defined type.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type u32)
  (type (record
    (field "foo" f64)
    (field "bar" bool)
    (field "baz" 0)
  ))
)
`));

// Record with invalid type index.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type u32)
  (type (record
    (field "baz" 1)
  ))
)
`)), WebAssembly.CompileError, /invalid type index/);

// Record referencing non-value type (func type).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32) (result s32)))
  (type (record (field "f" 0)))
)
`)), WebAssembly.CompileError, /not a value type/);

// Duplicate field names in a record - should fail (labels must be
// strongly-unique per spec).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" u32) (field "x" u32)))
)
`)), WebAssembly.CompileError, /not strongly-unique/);

// ----------------------------------------------------------------------------
// Variant types

// Basic variant.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (variant (case "ok" u32) (case "err" string)))
)
`));

// Variant with no-payload case.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (variant (case "none") (case "some" u32)))
)
`));

// Empty variant (invalid).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (variant))
)
`)), WebAssembly.CompileError, /at least one case/);

// Variant with invalid type reference.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (variant (case "bad" 99)))
)
`)), WebAssembly.CompileError, /invalid type index/);

// Duplicate case names in a variant.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (variant (case "a" u32) (case "a" u32)))
)
`)), WebAssembly.CompileError, /not strongly-unique/);

// ----------------------------------------------------------------------------
// List types

// Basic list.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (list u32))
)
`));

// List of a compound type.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" f64) (field "y" f64)))
  (type (list 0))
)
`));

// ----------------------------------------------------------------------------
// Tuple types

// Basic tuple.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (tuple u32 u32 f64))
)
`));

// Empty tuple (invalid).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (tuple))
)
`)), WebAssembly.CompileError, /at least one type/);

// Tuple with type reference.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "a" u32) (field "b" u32)))
  (type (tuple 0 u32 f64))
)
`));

// ----------------------------------------------------------------------------
// Flags types

// Basic flags.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (flags "read" "write" "execute"))
)
`));

// Empty flags (invalid).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (flags))
)
`)), WebAssembly.CompileError, /at least one label/);

// More than 32 flags (invalid).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (flags
    "a" "b" "c" "d" "e" "f" "g" "h"
    "i" "j" "k" "l" "m" "n" "o" "p"
    "q" "r" "s" "t" "u" "v" "w" "x"
    "y" "z" "aa" "bb" "cc" "dd" "ee" "ff"
    "gg"
  ))
)
`)), WebAssembly.CompileError, /too many labels/);

// Duplicate flag labels.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (flags "read" "read"))
)
`)), WebAssembly.CompileError, /not strongly-unique/);

// ----------------------------------------------------------------------------
// Enum types

// Basic enum.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (enum "red" "green" "blue"))
)
`));

// Empty enum (invalid).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (enum))
)
`)), WebAssembly.CompileError, /at least one case/);

// Duplicate enum labels.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (enum "red" "red"))
)
`)), WebAssembly.CompileError, /not strongly-unique/);

// ----------------------------------------------------------------------------
// Option types

// Basic option.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (option u32))
)
`));

// Option of a compound type.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" u32) (field "y" u32)))
  (type (option 0))
)
`));

// ----------------------------------------------------------------------------
// Result types

// Result with ok and error.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (result u32 (error string)))
)
`));

// Result with ok only.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (result u32))
)
`));

// Result with error only.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (result (error string)))
)
`));

// Result with neither.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (result))
)
`));

// ----------------------------------------------------------------------------
// Own and borrow types (resources not supported per plan)
// TODO(wasm-cm): Resource type parsing (0x3f) not yet implemented

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (resource (rep i32)))
  (type (own 0))
)
`)), WebAssembly.CompileError, /./);

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (resource (rep i32)))
  (type (borrow 0))
)
`)), WebAssembly.CompileError, /./);

// ----------------------------------------------------------------------------
// Func types

// Basic func type.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32) (param "b" s32) (result s32)))
)
`));

// Func with no result.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32)))
)
`));

// Func with no params.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (result s32)))
)
`));

// Func with no params or result.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func))
)
`));

// Func with compound param types.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record
    (field "foo" f64)
    (field "bar" bool)
  ))
  (type (func (param "a" 0) (param "b" 0)))
)
`));

// Func with compound result type.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" f64) (field "y" f64)))
  (type (func (param "a" f64) (param "b" f64) (result 0)))
)
`));

// Duplicate param names (invalid).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32) (param "a" s32)))
)
`)), WebAssembly.CompileError, /not strongly-unique/);

// ----------------------------------------------------------------------------
// Name well-formedness
// TODO(wasm-cm): Name validation not yet implemented.

// Labels must start with a letter, not a digit.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "0bad" u32)))
)
`)), WebAssembly.CompileError, /./);

// Labels cannot contain underscores.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "no_underscores" u32)))
)
`)), WebAssembly.CompileError, /./);

// Labels cannot contain spaces.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "no spaces" u32)))
)
`)), WebAssembly.CompileError, /./);

// Labels cannot be empty.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "" u32)))
)
`)), WebAssembly.CompileError, /./);

// Labels cannot have consecutive hyphens.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "no--double" u32)))
)
`)), WebAssembly.CompileError, /./);

// Labels cannot end with a hyphen.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "trailing-" u32)))
)
`)), WebAssembly.CompileError, /./);

// Edge cases

// Forward type reference - should fail.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" 1)))
  (type u32)
)
`)), WebAssembly.CompileError, /invalid type index/);

// Multiple type definitions referencing each other in order.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type u32)
  (type (record (field "a" 0) (field "b" f64)))
  (type (tuple 0 1))
  (type (list 1))
  (type (option 2))
  (type (func (param "x" 1) (param "y" 2) (result 0)))
)
`));

// Valid labels in record fields.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record
    (field "x" u32)
    (field "my-field" u32)
    (field "a0" u32)
    (field "get-HTTP-header" u32)
  ))
)
`));

// Invalid label in a record field.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "no_underscores" u32)))
)
`)), WebAssembly.CompileError, /./);

// Valid labels in func params.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "my-param" s32) (result s32)))
)
`));

// Invalid label in a func param.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "0starts-with-digit" s32) (result s32)))
)
`)), WebAssembly.CompileError, /./);

// Valid labels in variant cases.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (variant (case "ok" u32) (case "not-found") (case "HTTP-error" string)))
)
`));

// Invalid label in a variant case.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (variant (case "has space" u32)))
)
`)), WebAssembly.CompileError, /./);

// Valid labels in flags.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (flags "can-read" "can-write" "O-APPEND"))
)
`));

// Invalid label in flags.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (flags "trailing-"))
)
`)), WebAssembly.CompileError, /./);

// Valid labels in enums.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (enum "left" "top-right" "BOTTOM-LEFT"))
)
`));

// Invalid label in an enum.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (enum "" "ok"))
)
`)), WebAssembly.CompileError, /./);
