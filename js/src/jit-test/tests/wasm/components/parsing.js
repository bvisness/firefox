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
  1, 0, 0x0d, 0,
]));
// TODO: Test any introspection properties of the above component

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