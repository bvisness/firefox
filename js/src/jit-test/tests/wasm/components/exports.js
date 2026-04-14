// Export a function.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32) (param "b" s32) (result s32)))

  (core module
    (func (export "add_impl") (param i32 i32) (result i32)
      (i32.add (local.get 0) (local.get 1))
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "add_impl" (core func))
  (func (type 0) (canon lift (core func 0)))
  (export "add" (func 0))
)
`));

// Export a type.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record (field "x" f64) (field "y" f64)))
  (export "point" (type 0))
)
`));

// Export a core module.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module
    (func (export "add") (param i32 i32) (result i32)
      (i32.add (local.get 0) (local.get 1))
    )
  )
  (export "adder" (core module 0))
)
`));

// Export multiple items of different sorts.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module
    (func (export "add") (param i32 i32) (result i32)
      (i32.add (local.get 0) (local.get 1))
    )
  )
  (core module
    (func (export "sub") (param i32 i32) (result i32)
      (i32.sub (local.get 0) (local.get 1))
    )
  )

  (export "adder" (core module 0))
  (export "subber" (core module 1))
)
`));

// Invalid function index.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32) (param "b" s32) (result s32)))

  (core module
    (func (export "add_impl") (param i32 i32) (result i32)
      (i32.add (local.get 0) (local.get 1))
    )
  )
  (core instance (instantiate 0))
  (alias core export 0 "add_impl" (core func))
  (func (type 0) (canon lift (core func 0)))
  (export "add" (func 1))
)
`)), WebAssembly.CompileError, /invalid function index 1 for export/);

// Invalid type index.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type u32)
  (export "bad" (type 5))
)
`)), WebAssembly.CompileError, /invalid type index 5 for export/);

// Invalid core module index.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (export "bad" (core module 1))
)
`)), WebAssembly.CompileError, /invalid core module index 1 for export/);

// TODO(wasm-cm): Export name uniqueness validation not yet implemented.
// Duplicate export names should be rejected.

// Export a component - requires nested components (section ID 4) which aren't
// supported, so the component section itself is rejected.
// TODO(wasm-cm)
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (component)
  (export "inner" (component 0))
)
`)), WebAssembly.CompileError, /unexpected section ID/);

// Export a component instance - also requires nested components.
// TODO(wasm-cm)
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (component)
  (instance (instantiate 0))
  (export "inst" (instance 0))
)
`)), WebAssembly.CompileError, /unexpected section ID/);

// ---- Integration test ----
// A complete component exercising types, core modules, instances, aliases,
// canon lift, and exports together.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32) (param "b" s32) (result s32)))

  (core module
    (func (export "add_impl") (param i32 i32) (result i32)
      (i32.add (local.get 0) (local.get 1))
    )
  )
  (core module
    (func (export "sub_impl") (param i32 i32) (result i32)
      (i32.sub (local.get 0) (local.get 1))
    )
  )

  (core instance (instantiate 0))
  (core instance (instantiate 1))

  (alias core export 0 "add_impl" (core func))
  (alias core export 1 "sub_impl" (core func))
  (func (type 0) (canon lift (core func 0)))
  (func (type 0) (canon lift (core func 1)))

  (export "add" (func 0))
  (export "sub" (func 1))
)
`));
