// TODO(wasm-cm): Everything in here is nonstandard and should be bikeshedded
// internally and then eventually tested properly after discussion with the CG.
//
// Ideas for what to test later: calling without `new`, prototype chain,
// toString tag, instanceof, typeof, etc.

if (wasmComponentsEnabled()) {
  assertErrorMessage(() => new WebAssembly.Component(), TypeError, /1 argument required/);
  assertErrorMessage(() => new WebAssembly.Component(42), TypeError, /first argument must be an ArrayBuffer/);
} else {
  assertErrorMessage(() => new WebAssembly.Component(), TypeError, /not a constructor/);
}
