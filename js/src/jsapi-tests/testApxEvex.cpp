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

// Byte round-trip for the APX 3-operand NDD integer ops, encoded via the
// extended EVEX prefix in map 4 (Intel APX spec sec 3.1.2.3.1). Reference bytes
// were produced by GNU as 2.42, which already supports APX. This test only
// emits and compares bytes; it never executes the instructions, so it is safe
// to run on any host.
BEGIN_TEST(testApxEvex_ndd_encoding) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  // Binary ops: dst = src0 OP src1. gas `OP r19, r17, r18`.
  masm.addq(r17, r18, r19);
  masm.subq(r17, r18, r19);
  masm.andq(r17, r18, r19);
  masm.orq(r17, r18, r19);
  masm.xorq(r17, r18, r19);
  // Shifts by immediate: dst = src OP imm. gas `OP r19, r17, 5`.
  masm.shlq(Imm32(5), r17, r19);
  masm.shrq(Imm32(5), r17, r19);
  masm.sarq(Imm32(5), r17, r19);
  // Shifts by CL: dst = src OP cl. gas `OP r19, r17, cl`.
  masm.shlq(r17, rcx, r19);
  masm.shrq(r17, rcx, r19);
  masm.sarq(r17, rcx, r19);

  CHECK(!masm.oom());

  Linker linker(masm);
  JitCode* code = linker.newCode(cx, CodeKind::Other);
  CHECK(code);

  static const uint8_t expected[] = {
      0x62, 0xec, 0xe4, 0x10, 0x01, 0xd1,        // add r19, r17, r18
      0x62, 0xec, 0xe4, 0x10, 0x29, 0xd1,        // sub r19, r17, r18
      0x62, 0xec, 0xe4, 0x10, 0x21, 0xd1,        // and r19, r17, r18
      0x62, 0xec, 0xe4, 0x10, 0x09, 0xd1,        // or  r19, r17, r18
      0x62, 0xec, 0xe4, 0x10, 0x31, 0xd1,        // xor r19, r17, r18
      0x62, 0xfc, 0xe4, 0x10, 0xc1, 0xe1, 0x05,  // shl r19, r17, 5
      0x62, 0xfc, 0xe4, 0x10, 0xc1, 0xe9, 0x05,  // shr r19, r17, 5
      0x62, 0xfc, 0xe4, 0x10, 0xc1, 0xf9, 0x05,  // sar r19, r17, 5
      0x62, 0xfc, 0xe4, 0x10, 0xd3, 0xe1,        // shl r19, r17, cl
      0x62, 0xfc, 0xe4, 0x10, 0xd3, 0xe9,        // shr r19, r17, cl
      0x62, 0xfc, 0xe4, 0x10, 0xd3, 0xf9,        // sar r19, r17, cl
  };

  CHECK(code->instructionsSize() >= sizeof(expected));
  bool matched = memcmp(code->raw(), expected, sizeof(expected)) == 0;
  if (!matched) {
    const uint8_t* got = code->raw();
    fprintf(stderr, "APX NDD encoding mismatch:\nexpected:");
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
END_TEST(testApxEvex_ndd_encoding)

// Execute the NDD ops and confirm both the result and that the sources are not
// modified. Requires APX to actually be available (e.g. under `sde64 -future
// --enable-apx`), so it is skipped on hosts without APX to avoid #UD.
BEGIN_TEST(testApxEvex_ndd_execute) {
  if (!CPUInfo::IsAPXPresent()) {
    return true;  // Skip: no APX on this host.
  }

  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  // Outputs: [add, sub, and, or, xor, shl_imm, shr_imm, sar_imm, shl_cl,
  // sar_neg,
  //           src0_after, src1_after].
  uint64_t out[12] = {};

  PrepareJit(masm);

  // Binary ops with src0=0xC, src1=0xA (rax = src0, rdx = src1).
  masm.movePtr(ImmWord(0xC), rax);
  masm.movePtr(ImmWord(0xA), rdx);
  masm.addq(rax, rdx, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[0]));  // 0x16
  masm.subq(rax, rdx, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[1]));  // 0x2
  masm.andq(rax, rdx, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[2]));  // 0x8
  masm.orq(rax, rdx, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[3]));  // 0xE
  masm.xorq(rax, rdx, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[4]));  // 0x6
  // Sources must be unchanged (non-destructive).
  masm.storePtr(rax, AbsoluteAddress(&out[10]));  // 0xC
  masm.storePtr(rdx, AbsoluteAddress(&out[11]));  // 0xA

  // Shifts by immediate: src = 0x100.
  masm.movePtr(ImmWord(0x100), rax);
  masm.shlq(Imm32(4), rax, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[5]));  // 0x1000
  masm.shrq(Imm32(4), rax, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[6]));  // 0x10
  masm.sarq(Imm32(4), rax, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[7]));  // 0x10

  // Shift by CL: src = 0x100, cl = 4.
  masm.movePtr(ImmWord(4), rcx);
  masm.shlq(rax, rcx, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[8]));  // 0x1000

  // Arithmetic right shift of a negative value: src = -0x100, sar 4 = -0x10.
  masm.movePtr(ImmWord(uint64_t(-0x100)), rax);
  masm.sarq(Imm32(4), rax, rbx);
  masm.storePtr(rbx, AbsoluteAddress(&out[9]));  // -0x10

  CHECK(ExecuteJit(cx, masm));

  CHECK(out[0] == 0x16);
  CHECK(out[1] == 0x2);
  CHECK(out[2] == 0x8);
  CHECK(out[3] == 0xE);
  CHECK(out[4] == 0x6);
  CHECK(out[5] == 0x1000);
  CHECK(out[6] == 0x10);
  CHECK(out[7] == 0x10);
  CHECK(out[8] == 0x1000);
  CHECK(int64_t(out[9]) == -0x10);
  CHECK(out[10] == 0xC);  // src0 unchanged
  CHECK(out[11] == 0xA);  // src1 unchanged

  return true;
}
END_TEST(testApxEvex_ndd_execute)

// Byte spot-check that legacy GPR ops gain a REX2 prefix when an extended
// register (r16-r31) is used. push/pop are map-0 ops whose encoding is
// unambiguous; the reference bytes are from GNU as 2.42.
BEGIN_TEST(testApxRex2_pushpop_encoding) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  masm.push(r16);  // d5 10 50
  masm.pop(r17);   // d5 10 59
  masm.push(r31);  // d5 11 57
  masm.pop(r8);    // 41 58  (legacy REX, no REX2 needed)

  CHECK(!masm.oom());
  Linker linker(masm);
  JitCode* code = linker.newCode(cx, CodeKind::Other);
  CHECK(code);

  static const uint8_t expected[] = {
      0xd5, 0x10, 0x50,  // push r16
      0xd5, 0x10, 0x59,  // pop r17
      0xd5, 0x11, 0x57,  // push r31
      0x41, 0x58,        // pop r8
  };
  CHECK(code->instructionsSize() >= sizeof(expected));
  CHECK(memcmp(code->raw(), expected, sizeof(expected)) == 0);
  return true;
}
END_TEST(testApxRex2_pushpop_encoding)

// Execute a chain of ordinary GPR ops (mov-immediate, mov reg-reg, add,
// push/pop, load/store) using extended registers r16-r23, confirming the
// REX2-promoted legacy encodings run correctly. Requires APX (skipped
// otherwise).
BEGIN_TEST(testApxRex2_execute) {
  if (!CPUInfo::IsAPXPresent()) {
    return true;  // Skip: no APX on this host.
  }

  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  uint64_t out[3] = {};
  uint64_t scratch = 0;

  PrepareJit(masm);
  masm.movePtr(ImmWord(0x1111), r16);  // mov-immediate into high reg
  masm.movePtr(ImmWord(0x2222), r17);
  masm.movePtr(r16, r18);  // mov reg-reg (high <- high)
  masm.addPtr(r17, r18);   // r18 = 0x3333 (2-operand add, high regs)
  masm.storePtr(r18, AbsoluteAddress(&out[0]));

  // Round-trip a high register through memory (store + load).
  masm.storePtr(r16, AbsoluteAddress(&scratch));
  masm.loadPtr(AbsoluteAddress(&scratch), r19);
  masm.storePtr(r19, AbsoluteAddress(&out[1]));

  // push/pop a high register.
  masm.push(r16);
  masm.pop(r20);
  masm.storePtr(r20, AbsoluteAddress(&out[2]));

  CHECK(ExecuteJit(cx, masm));

  CHECK(out[0] == 0x3333);
  CHECK(out[1] == 0x1111);
  CHECK(out[2] == 0x1111);
  return true;
}
END_TEST(testApxRex2_execute)

#endif  // defined(ENABLE_APX_EXPERIMENT) && defined(JS_CODEGEN_X64)
