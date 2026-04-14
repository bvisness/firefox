// Core modules

// Empty core module.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
)
`));

// Multiple core modules.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (core module)
)
`));

// Core module with exports of every kind.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module
    (func (export "func"))
    (table (export "table") 0 0 funcref)
    (memory (export "memory") 0 0)
    (global (export "global") i32 (i32.const 0))
    (tag (export "tag"))
  )
)
`));

// Core instances (instantiate)

// Basic instantiation.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (core instance (instantiate 0))
)
`));

// Invalid module index.
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (core instance (instantiate 1))
)
`)), WebAssembly.CompileError, /invalid core module index 1/);

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (core instance (instantiate 99))
)
`)), WebAssembly.CompileError, /invalid core module index 99/);

// Multiple instances from different modules.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (core module)
  (core instance (instantiate 0))
  (core instance (instantiate 1))
)
`));

// Multiple instances from the same module.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (core instance (instantiate 0))
  (core instance (instantiate 0))
)
`));

// Core module with actual code, instantiated.
new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module
    (func (export "add") (param i32 i32) (result i32)
      (i32.add (local.get 0) (local.get 1))
    )
  )
  (core instance (instantiate 0))
)
`));

// Instantiation with import args: a module that imports from another instance.
// TODO(wasm-cm): Add a test with (with "name" (instance N)) once import
// validation is more complete.

// Inline exports (TODO path).
assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module
    (func (export "f"))
  )
  (core instance (instantiate 0))
  (alias core export 0 "f" (core func))
  (core instance (export "f" (func 0)))
)
`)), WebAssembly.CompileError, /TODO/);
