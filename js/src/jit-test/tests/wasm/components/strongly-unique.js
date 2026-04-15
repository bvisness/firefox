// We use record fields as our oracle for strongly-uniqueness.
function assertAllStronglyUnique(names) {
  new WebAssembly.Component(wasmTextToBinary(`(component
    (type (record
      ${names.map(n => `(field "${n}" bool)`).join("\n")}
    ))
  )`));
}
function assertNotStronglyUnique(okNames, badName) {
  assertAllStronglyUnique(okNames);
  assertErrorMessage(() => new WebAssembly.Component(wasmTextToBinary(`(component
    (type (record
      ${okNames.map(n => `(field "${n}" bool)`).join("\n")}
      (field "${badName}" bool)
    ))
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
