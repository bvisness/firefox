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
