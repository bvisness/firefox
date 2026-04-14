// Helper: builds a component that defines a component func type, provides a
// core module with a core function of the given core signature, aliases and
// lifts it.
function componentWithLift(componentFuncType, coreParams, coreResults) {
  const coreParamStr = coreParams.map(t => `${t}`).join(" ");
  const coreResultStr = coreResults.map(t => `${t}`).join(" ");

  // Build a core function body that returns appropriate defaults.
  let body = "";
  for (const r of coreResults) {
    switch (r) {
      case "i32": body += "(i32.const 0) "; break;
      case "i64": body += "(i64.const 0) "; break;
      case "f32": body += "(f32.const 0) "; break;
      case "f64": body += "(f64.const 0) "; break;
    }
  }

  let paramSection = coreParams.length > 0
    ? `(param ${coreParamStr})`
    : "";
  let resultSection = coreResults.length > 0
    ? `(result ${coreResultStr})`
    : "";

  return wasmTextToBinary(`
    (component
      (type ${componentFuncType})

      (core module
        (func (export "f") ${paramSection} ${resultSection}
          ${body}
        )
      )
      (core instance (instantiate 0))
      (alias core export 0 "f" (core func))
      (func (type 0) (canon lift (core func 0)))
    )
  `);
}

// ---- Canon lift: primitive types ----

// bool -> i32
new WebAssembly.Component(componentWithLift(
  `(func (param "a" bool) (result bool))`,
  ["i32"], ["i32"]
));

// s8, s16, s32 -> i32
for (const t of ["s8", "s16", "s32"]) {
  new WebAssembly.Component(componentWithLift(
    `(func (param "a" ${t}) (result ${t}))`,
    ["i32"], ["i32"]
  ));
}

// u8, u16, u32 -> i32
for (const t of ["u8", "u16", "u32"]) {
  new WebAssembly.Component(componentWithLift(
    `(func (param "a" ${t}) (result ${t}))`,
    ["i32"], ["i32"]
  ));
}

// s64, u64 -> i64
for (const t of ["s64", "u64"]) {
  new WebAssembly.Component(componentWithLift(
    `(func (param "a" ${t}) (result ${t}))`,
    ["i64"], ["i64"]
  ));
}

// f32 -> f32
new WebAssembly.Component(componentWithLift(
  `(func (param "a" f32) (result f32))`,
  ["f32"], ["f32"]
));

// f64 -> f64
new WebAssembly.Component(componentWithLift(
  `(func (param "a" f64) (result f64))`,
  ["f64"], ["f64"]
));

// char -> i32
new WebAssembly.Component(componentWithLift(
  `(func (param "a" char) (result char))`,
  ["i32"], ["i32"]
));

// string -> (i32, i32) for pointer + length
new WebAssembly.Component(componentWithLift(
  `(func (param "a" string) (result string))`,
  ["i32", "i32"], ["i32", "i32"]
));

// ---- Canon lift: compound types ----

// Record: fields flatten to their individual core types.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" f64) (field "y" f64) (field "z" f64)))
  (type (func (param "pos" 0) (result 0)))

  (core module
    (func (export "f") (param f64 f64 f64) (result f64 f64 f64)
      (local.get 0) (local.get 1) (local.get 2)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 1) (canon lift (core func 0)))
)
`));

// Nested record: inner record fields also flatten.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" f64) (field "y" f64)))
  (type (record (field "start" 0) (field "end" 0)))
  (type (func (param "seg" 1) (result u32)))

  (core module
    (func (export "f") (param f64 f64 f64 f64) (result i32)
      (i32.const 0)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 2) (canon lift (core func 0)))
)
`));

// Tuple: elements flatten like record fields.
// TODO(wasm-cm): Currently fails at type parsing, not flattening.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (tuple u32 f64 u32))
  (type (func (param "t" 0) (result 0)))

  (core module
    (func (export "f") (param i32 f64 i32) (result i32 f64 i32)
      (local.get 0) (local.get 1) (local.get 2)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 1) (canon lift (core func 0)))
)
`));

// List: flattens to (i32, i32) for pointer + length.
// TODO(wasm-cm): Currently fails at type parsing, not flattening.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (list u32))
  (type (func (param "items" 0) (result 0)))

  (core module
    (func (export "f") (param i32 i32) (result i32 i32)
      (local.get 0) (local.get 1)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 1) (canon lift (core func 0)))
)
`));

// Flags: flattens to i32.
// TODO(wasm-cm): Currently fails at type parsing, not flattening.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (flags "read" "write" "execute"))
  (type (func (param "perms" 0) (result 0)))

  (core module
    (func (export "f") (param i32) (result i32)
      (local.get 0)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 1) (canon lift (core func 0)))
)
`));

// Enum: flattens to i32 (discriminant).
// TODO(wasm-cm): Currently fails at type parsing. Once type parsing is
// implemented, will hit MOZ_CRASH("TODO") in FlattenType.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (enum "red" "green" "blue"))
  (type (func (param "color" 0) (result 0)))

  (core module
    (func (export "f") (param i32) (result i32)
      (local.get 0)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 1) (canon lift (core func 0)))
)
`));

// Variant: flattens to discriminant + payload.
// TODO(wasm-cm): Currently fails at type parsing. Once type parsing is
// implemented, will hit MOZ_CRASH("TODO") in FlattenType.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (variant (case "none") (case "some" u32)))
  (type (func (param "v" 0) (result 0)))

  (core module
    (func (export "f") (param i32 i32) (result i32 i32)
      (local.get 0) (local.get 1)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 1) (canon lift (core func 0)))
)
`));

// Option: flattens to discriminant (i32) + payload.
// TODO(wasm-cm): Currently fails at type parsing. Once type parsing is
// implemented, will hit MOZ_CRASH("TODO") in FlattenType.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (option u32))
  (type (func (param "v" 0) (result 0)))

  (core module
    (func (export "f") (param i32 i32) (result i32 i32)
      (local.get 0) (local.get 1)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 1) (canon lift (core func 0)))
)
`));

// Result: flattens to discriminant + ok payload + error payload.
// TODO(wasm-cm): Currently fails at type parsing. Once type parsing is
// implemented, will hit MOZ_CRASH("TODO") in FlattenType.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (result u32 (error u32)))
  (type (func (param "v" 0) (result 0)))

  (core module
    (func (export "f") (param i32 i32) (result i32 i32)
      (local.get 0) (local.get 1)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 1) (canon lift (core func 0)))
)
`));

// ---- Canon lift: signature mismatch ----

// Too few core params.
assertErrorMessage(() => new WebAssembly.Component(componentWithLift(
  `(func (param "a" s32) (param "b" s32) (result s32))`,
  ["i32"], ["i32"]
)), WebAssembly.CompileError, /could not lift core func/);

// Too many core params.
assertErrorMessage(() => new WebAssembly.Component(componentWithLift(
  `(func (param "a" s32) (result s32))`,
  ["i32", "i32", "i32"], ["i32"]
)), WebAssembly.CompileError, /could not lift core func/);

// Wrong core param type: component expects s64 (i64), core has i32.
assertErrorMessage(() => new WebAssembly.Component(componentWithLift(
  `(func (param "a" s64) (result s64))`,
  ["i32"], ["i32"]
)), WebAssembly.CompileError, /could not lift core func/);

// Missing core result.
assertErrorMessage(() => new WebAssembly.Component(componentWithLift(
  `(func (param "a" s32) (result s32))`,
  ["i32"], []
)), WebAssembly.CompileError, /could not lift core func/);

// Extra core result.
assertErrorMessage(() => new WebAssembly.Component(componentWithLift(
  `(func (param "a" s32))`,
  ["i32"], ["i32"]
)), WebAssembly.CompileError, /could not lift core func/);

// String param count mismatch: string needs (i32, i32), core only has one i32.
assertErrorMessage(() => new WebAssembly.Component(componentWithLift(
  `(func (param "a" string) (result bool))`,
  ["i32"], ["i32"]
)), WebAssembly.CompileError, /could not lift core func/);

// ---- Canon lift: type validation ----

// Lift with non-func type (record).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" u32)))

  (core module (func (export "f") (param i32) (result i32) (local.get 0)))
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 0) (canon lift (core func 0)))
)
`)), WebAssembly.CompileError, /canon lift requires a func type/);

// Invalid core func index.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32) (result s32)))

  (core module (func (export "f") (param i32) (result i32) (local.get 0)))
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 0) (canon lift (core func 99)))
)
`)), WebAssembly.CompileError, /invalid core function index/);

// ---- Canon lift: complex signatures ----

// Mixed param types: string + u32 -> bool
// string flattens to (i32, i32), u32 to i32, bool to i32
new WebAssembly.Component(componentWithLift(
  `(func (param "a" string) (param "b" u32) (result bool))`,
  ["i32", "i32", "i32"], ["i32"]
));

// Record param via type reference.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" u32) (field "y" f64)))
  (type (func (param "pt" 0) (result bool)))

  (core module
    (func (export "f") (param i32 f64) (result i32)
      (i32.const 0)
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 1) (canon lift (core func 0)))
)
`));

// No params, no results.
new WebAssembly.Component(componentWithLift(
  `(func)`,
  [], []
));

// ---- Canon lower ----
// TODO(wasm-cm): Canon lower not yet implemented.

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32) (result s32)))

  (core module (func (export "f") (param i32) (result i32) (local.get 0)))
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (func (type 0) (canon lift (core func 0)))
  (core func (canon lower (func 0)))
)
`)), WebAssembly.CompileError, /TODO/);
