// JS API basics (minimal - the JS API is non-standard and invented for testing)
assertErrorMessage(() => new WebAssembly.Component(), TypeError, /1 argument required/);
assertErrorMessage(() => new WebAssembly.Component(42), TypeError, /first argument must be an ArrayBuffer/);
// TODO(wasm-cm): Test calling without `new`, prototype chain, toString tag,
// instanceof, typeof, etc. once the JS API is more settled.

// Preamble parsing

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
  0, 0, 0, 0,
])), WebAssembly.CompileError, /binary version .* does not match/);

// Core module version should be rejected by the Component constructor.
assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
  1, 0, 0, 0,
])), WebAssembly.CompileError, /binary version .* does not match/);

// Valid empty component.
new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
  0x0d, 0, 1, 0,
]));

// Section framing errors

// Section length extends past end of component.
assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
  0x0d, 0, 1, 0,

  0x01, 0x10, // core module section, section length too long
    0x00, 0x61, 0x73, 0x6D,
    0x01, 0x00, 0x00, 0x00,
])), WebAssembly.CompileError, /invalid section length/);

// Unknown section ID.
assertErrorMessage(() => new WebAssembly.Component(new Uint8Array([
  0, 0x61, 0x73, 0x6D,
  0x0d, 0, 1, 0,

  0xFF, 0x00, // unknown section ID 0xFF, length 0
])), WebAssembly.CompileError, /unexpected section ID/);
