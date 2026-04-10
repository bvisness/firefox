assertErrorMessage(() => new WebAssembly.Component(), TypeError, /1 argument required/);
assertErrorMessage(() => new WebAssembly.Component(42), TypeError, /first argument must be an ArrayBuffer/);
assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0,
])), WebAssembly.CompileError, /failed to match magic number/);
assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0, 0, 0, 0,
])), WebAssembly.CompileError, /failed to match magic number/);
assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
])), WebAssembly.CompileError, /failed to read version/);
assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
])), WebAssembly.CompileError, /failed to read version/);
assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
  0, 0, 0, 0,
])), WebAssembly.CompileError, /binary version .* does not match/);
assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
  1, 0, 0, 0,
])), WebAssembly.CompileError, /binary version .* does not match/);

new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
  0x0d, 0, 1, 0,
]));
// TODO: Test any introspection properties of the above component

assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
  0x0d, 0, 1, 0,

  0x01, 0x10, // core module section, section length too long
    0x00, 0x61, 0x73, 0x6D,
    0x01, 0x00, 0x00, 0x00,
])), WebAssembly.CompileError, /invalid section length/);

new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
)
`));
// TODO: Test any introspection properties of the above component

new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (core instance (instantiate 0))
)
`));
// TODO: Test any introspection properties of the above component

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (core instance (instantiate 1))
)
`)), WebAssembly.CompileError, /invalid core module index 1/);
// TODO: Test any introspection properties of the above component

new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module)
  (core module)
  (core instance (instantiate 0))
  (core instance (instantiate 1))
)
`));
// TODO: Test any introspection properties of the above component

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
// TODO: Test any introspection properties of the above component

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

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (type u32)
  (type (record
    (field "baz" 1)
  ))
)
`)), WebAssembly.CompileError, /invalid type index/);

new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (func (param "a" s32) (param "b" s32) (result s32)))
)
`));

new WebAssembly.Component(wasmTextToBinary(`
(component
  (type (record
    (field "foo" f64)
    (field "bar" bool)
  ))
  (type (func (param "a" 0) (param "b" 0)))
)
`));

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module
    (func (export "add_impl") (param i32 i32) (result i32)
      (i32.add (local.get 0) (local.get 1))
    )
  )
  (core instance (instantiate 0))

  (alias core export 1 "add_impl" (core func))
)
`)), WebAssembly.CompileError, /invalid core instance index/);

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module
    (func (export "add_impl") (param i32 i32) (result i32)
      (i32.add (local.get 0) (local.get 1))
    )
  )
  (core instance (instantiate 0))

  (alias core export 0 "pizza" (core func))
)
`)), WebAssembly.CompileError, /core instance 0 has no export "pizza"/);

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module (global (export "add_impl") i32 (i32.const 0)))
  (core instance (instantiate 0))

  (alias core export 0 "add_impl" (core func))
)
`)), WebAssembly.CompileError, /export "add_impl" of core instance 0 is not a function/);

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module (global (export "add_impl") i32 (i32.const 0)))
  (core instance (instantiate 0))

  (alias core export 0 "add_impl" (core table))
)
`)), WebAssembly.CompileError, /export "add_impl" of core instance 0 is not a table/);

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module (global (export "add_impl") i32 (i32.const 0)))
  (core instance (instantiate 0))

  (alias core export 0 "add_impl" (core memory))
)
`)), WebAssembly.CompileError, /export "add_impl" of core instance 0 is not a memory/);

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module (tag (export "add_impl")))
  (core instance (instantiate 0))

  (alias core export 0 "add_impl" (core global))
)
`)), WebAssembly.CompileError, /export "add_impl" of core instance 0 is not a global/);

assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module (global (export "add_impl") i32 (i32.const 0)))
  (core instance (instantiate 0))

  (alias core export 0 "add_impl" (core tag))
)
`)), WebAssembly.CompileError, /export "add_impl" of core instance 0 is not a tag/);

new WebAssembly.Component(wasmTextToBinary(`
(component
  (core module
    (func (export "func"))
    (table (export "table") 0 0 funcref)
    (memory (export "memory") 0 0)
    (global (export "global") i32 (i32.const 0))
    (tag (export "tag"))
  )
  (core instance (instantiate 0))

  (alias core export 0 "func" (core func))
  (alias core export 0 "table" (core table))
  (alias core export 0 "memory" (core memory))
  (alias core export 0 "global" (core global))
  (alias core export 0 "tag" (core tag))
)
`));

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
// TODO: Test any introspection properties of the above component
