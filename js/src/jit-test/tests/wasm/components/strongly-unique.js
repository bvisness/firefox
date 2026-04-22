// We use imported functions as our oracle for strongly-uniqueness, as only
// functions allow the full range of plain names.
function assertAllStronglyUnique(names) {
  new WebAssembly.Component(wasmTextToBinary(`(component
    ${names.map(n => `(import "${n}" (func))`).join("\n")}
  )`));
}
function assertNotStronglyUnique(okNames, badName) {
  assertAllStronglyUnique(okNames);
  assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`(component
    ${okNames.map(n => `(import "${n}" (func))`).join("\n")}
    (import "${badName}" (func))
  )`)), WebAssembly.CompileError, /not strongly-unique/);
}

const specOkExamples = [
  "foo", "foo-bar",
  "[constructor]foo",
  "[method]foo.bar", "[method]foo.baz",
];
assertAllStronglyUnique(specOkExamples);

assertNotStronglyUnique(specOkExamples, "foo");
assertNotStronglyUnique(specOkExamples, "foo-BAR");
assertNotStronglyUnique(specOkExamples, "[constructor]foo-BAR");
assertNotStronglyUnique(specOkExamples, "[method]foo.foo");
assertNotStronglyUnique(specOkExamples, "[method]foo.BAR");
