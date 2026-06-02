/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/Linker.h"
#include "jit/MacroAssembler.h"

#include "jsapi-tests/tests.h"
#include "jsapi-tests/testsJit.h"

#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;

#if defined(ENABLE_APX_EXPERIMENT) && defined(JS_CODEGEN_X64)

// Byte round-trip for the APX 3-operand NDD add. addq(src0, src1, dst) is encoded
// via the extended EVEX prefix in map 4 (Intel APX spec sec 3.1.2.3.1). The
// reference bytes were produced by GNU as 2.42 (`add dst, src0, src1`), which
// already supports APX. This test only emits and compares bytes; it never executes
// the instructions, so it is safe to run on any host.
BEGIN_TEST(testApxEvex_addq_rrr_encoding) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  masm.addq(r17, r18, r19);  // add r19, r17, r18
  masm.addq(rax, rcx, rdx);  // add rdx, rax, rcx
  masm.addq(r8, r9, r10);    // add r10, r8, r9
  masm.addq(rsp, rbp, rax);  // add rax, rsp, rbp
  masm.addq(r31, r16, rax);  // add rax, r31, r16
  masm.addq(rax, r31, r16);  // add r16, rax, r31

  CHECK(!masm.oom());

  Linker linker(masm);
  JitCode* code = linker.newCode(cx, CodeKind::Other);
  CHECK(code);

  static const uint8_t expected[] = {
      0x62, 0xec, 0xe4, 0x10, 0x01, 0xd1,  // add r19, r17, r18
      0x62, 0xf4, 0xec, 0x18, 0x01, 0xc8,  // add rdx, rax, rcx
      0x62, 0x54, 0xac, 0x18, 0x01, 0xc8,  // add r10, r8, r9
      0x62, 0xf4, 0xfc, 0x18, 0x01, 0xec,  // add rax, rsp, rbp
      0x62, 0xcc, 0xfc, 0x18, 0x01, 0xc7,  // add rax, r31, r16
      0x62, 0x64, 0xfc, 0x10, 0x01, 0xf8,  // add r16, rax, r31
  };

  CHECK(code->instructionsSize() >= sizeof(expected));
  bool matched = memcmp(code->raw(), expected, sizeof(expected)) == 0;
  if (!matched) {
    const uint8_t* got = code->raw();
    fprintf(stderr, "APX addq_rrr encoding mismatch:\nexpected:");
    for (size_t i = 0; i < sizeof(expected); i++) {
      fprintf(stderr, " %02x", expected[i]);
    }
    fprintf(stderr, "\ngot:     ");
    for (size_t i = 0; i < sizeof(expected); i++) {
      fprintf(stderr, " %02x", got[i]);
    }
    fprintf(stderr, "\n");
  }
  CHECK(matched);

  return true;
}
END_TEST(testApxEvex_addq_rrr_encoding)

// Execute an EVEX-promoted NDD add and confirm it computes dst = src0 + src1
// without modifying the sources. This requires APX to actually be available
// (e.g. running under `sde64 -future --enable-apx`), so it is skipped on hosts
// without APX to avoid #UD.
BEGIN_TEST(testApxEvex_addq_rrr_execute) {
  if (!CPUInfo::IsAPXPresent()) {
    return true;  // Skip: no APX on this host.
  }

  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  uint64_t out[3] = {0, 0, 0};

  PrepareJit(masm);
  masm.movePtr(ImmWord(0x1111), rax);  // src0
  masm.movePtr(ImmWord(0x2222), rcx);  // src1
  masm.addq(rax, rcx, rdx);            // rdx = rax + rcx (non-destructive)
  masm.storePtr(rdx, AbsoluteAddress(&out[0]));
  masm.storePtr(rax, AbsoluteAddress(&out[1]));
  masm.storePtr(rcx, AbsoluteAddress(&out[2]));
  CHECK(ExecuteJit(cx, masm));

  CHECK(out[0] == 0x3333);  // sum
  CHECK(out[1] == 0x1111);  // src0 unchanged
  CHECK(out[2] == 0x2222);  // src1 unchanged

  return true;
}
END_TEST(testApxEvex_addq_rrr_execute)

#endif  // defined(ENABLE_APX_EXPERIMENT) && defined(JS_CODEGEN_X64)
