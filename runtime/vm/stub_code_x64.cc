// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_X64)

#include "vm/code_generator.h"
#include "vm/compiler.h"
#include "vm/ic_data.h"
#include "vm/object_store.h"
#include "vm/pages.h"
#include "vm/resolver.h"
#include "vm/scavenger.h"
#include "vm/stub_code.h"


#define __ assembler->

namespace dart {

DEFINE_FLAG(bool, inline_alloc, true, "Inline allocation of objects.");
DEFINE_FLAG(bool, use_slow_path, false,
    "Set to true for debugging & verifying the slow paths.");

// Input parameters:
//   RSP : points to return address.
//   RSP + 8 : address of last argument in argument array.
//   RSP + 8*R10 : address of first argument in argument array.
//   RSP + 8*R10 + 8 : address of return value.
//   RBX : address of the runtime function to call.
//   R10 : number of arguments to the call.
// Must preserve callee saved registers R12 and R13.
static void GenerateCallRuntimeStub(Assembler* assembler) {
  ASSERT((R12 != CTX) && (R13 != CTX));
  const intptr_t isolate_offset = NativeArguments::isolate_offset();
  const intptr_t argc_offset = NativeArguments::argc_offset();
  const intptr_t argv_offset = NativeArguments::argv_offset();
  const intptr_t retval_offset = NativeArguments::retval_offset();

  __ EnterFrame(0);

  // Load current Isolate pointer from Context structure into RAX.
  __ movq(RAX, FieldAddress(CTX, Context::isolate_offset()));

  // Save exit frame information to enable stack walking as we are about
  // to transition to Dart VM C++ code.
  __ movq(Address(RAX, Isolate::top_exit_frame_info_offset()), RSP);

  // Save current Context pointer into Isolate structure.
  __ movq(Address(RAX, Isolate::top_context_offset()), CTX);

  // Cache Isolate pointer into CTX while executing runtime code.
  __ movq(CTX, RAX);

  // Reserve space for arguments and align frame before entering C++ world.
  __ AddImmediate(RSP, Immediate(-sizeof(NativeArguments)));
  if (OS::ActivationFrameAlignment() > 0) {
    __ andq(RSP, Immediate(~(OS::ActivationFrameAlignment() - 1)));
  }

  // Pass NativeArguments structure by value and call runtime.
  __ movq(Address(RSP, isolate_offset), CTX);  // Set isolate in NativeArgs.
  __ movq(Address(RSP, argc_offset), R10);  // Set argc in NativeArguments.
  __ leaq(RAX, Address(RBP, R10, TIMES_8, 1 * kWordSize));  // Compute argv.
  __ movq(Address(RSP, argv_offset), RAX);  // Set argv in NativeArguments.
  __ addq(RAX, Immediate(1 * kWordSize));  // Retval is next to 1st argument.
  __ movq(Address(RSP, retval_offset), RAX);  // Set retval in NativeArguments.
  __ call(RBX);

  // Reset exit frame information in Isolate structure.
  __ movq(Address(CTX, Isolate::top_exit_frame_info_offset()), Immediate(0));

  // Load Context pointer from Isolate structure into RBX.
  __ movq(RBX, Address(CTX, Isolate::top_context_offset()));

  // Reset Context pointer in Isolate structure.
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));
  __ movq(Address(CTX, Isolate::top_context_offset()), raw_null);

  // Cache Context pointer into CTX while executing Dart code.
  __ movq(CTX, RBX);

  __ LeaveFrame();
  __ ret();
}


// Input parameters:
//   RSP : points to return address.
//   RSP + 8 : address of last argument in argument array.
//   RSP + 8*R10 : address of first argument in argument array.
//   RSP + 8*R10 + 8 : address of return value.
//   RBX : address of the runtime function to call.
//   R10 : number of arguments to the call.
// Must preserve callee saved registers R12 and R13.
void StubCode::GenerateDartCallToRuntimeStub(Assembler* assembler) {
  GenerateCallRuntimeStub(assembler);
}


// Input parameters:
//   RSP : points to return address.
//   RSP + 8 : address of last argument in argument array.
//   RSP + 8*R10 : address of first argument in argument array.
//   RSP + 8*R10 + 8 : address of return value.
//   RBX : address of the runtime function to call.
//   R10 : number of arguments to the call.
// Must preserve callee saved registers R12 and R13.
void StubCode::GenerateStubCallToRuntimeStub(Assembler* assembler) {
  GenerateCallRuntimeStub(assembler);
}


// Print the stop message.
static void PrintStopMessage(const char* message) {
  OS::Print("Stop message: %s\n", message);
}


// Input parameters:
//   RSP : points to return address.
//   RDI : stop message (const char*).
// Must preserve all registers, except RDI and TMP.
void StubCode::GeneratePrintStopMessageStub(Assembler* assembler) {
  // Preserve caller-saved registers.
  __ pushq(RAX);
  __ pushq(RCX);
  __ pushq(RDX);
  __ pushq(RSI);
  __ pushq(R8);
  __ pushq(R9);
  __ pushq(R10);

  __ EnterFrame(0);

  // Align frame before entering C++ world.
  if (OS::ActivationFrameAlignment() > 0) {
    __ andq(RSP, Immediate(~(OS::ActivationFrameAlignment() - 1)));
  }

  // Stop message is already in RDI.
  __ movq(TMP, Immediate(reinterpret_cast<uword>(&PrintStopMessage)));
  __ call(TMP);

  __ LeaveFrame();

  // Restore caller-saved registers.
  __ popq(R10);
  __ popq(R9);
  __ popq(R8);
  __ popq(RSI);
  __ popq(RDX);
  __ popq(RCX);
  __ popq(RAX);

  __ ret();
}


// Input parameters:
//   RSP : points to return address.
//   RSP + 8 : address of return value.
//   RAX : address of first argument in argument array.
//   RAX - 8*R10 + 8 : address of last argument in argument array.
//   RBX : address of the native function to call.
//   R10 : number of arguments to the call.
// Uses R8.
void StubCode::GenerateCallNativeCFunctionStub(Assembler* assembler) {
  const intptr_t native_args_struct_offset = 0;
  const intptr_t isolate_offset =
      NativeArguments::isolate_offset() + native_args_struct_offset;
  const intptr_t argc_offset =
      NativeArguments::argc_offset() + native_args_struct_offset;
  const intptr_t argv_offset =
      NativeArguments::argv_offset() + native_args_struct_offset;
  const intptr_t retval_offset =
      NativeArguments::retval_offset() + native_args_struct_offset;

  __ EnterFrame(0);

  // Load current Isolate pointer from Context structure into R8.
  __ movq(R8, FieldAddress(CTX, Context::isolate_offset()));

  // Save exit frame information to enable stack walking as we are about
  // to transition to native code.
  __ movq(Address(R8, Isolate::top_exit_frame_info_offset()), RSP);

  // Save current Context pointer into Isolate structure.
  __ movq(Address(R8, Isolate::top_context_offset()), CTX);

  // Cache Isolate pointer into CTX while executing native code.
  __ movq(CTX, R8);

  // Reserve space for the native arguments structure passed on the stack (the
  // outgoing pointer parameter to the native arguments structure is passed in
  // RDI) and align frame before entering the C++ world.
  __ AddImmediate(RSP, Immediate(-sizeof(NativeArguments)));
  if (OS::ActivationFrameAlignment() > 0) {
    __ andq(RSP, Immediate(~(OS::ActivationFrameAlignment() - 1)));
  }

  // Pass NativeArguments structure by value and call native function.
  __ movq(Address(RSP, isolate_offset), CTX);  // Set isolate in NativeArgs.
  __ movq(Address(RSP, argc_offset), R10);  // Set argc in NativeArguments.
  __ movq(Address(RSP, argv_offset), RAX);  // Set argv in NativeArguments.
  __ leaq(RAX, Address(RBP, 2 * kWordSize));  // Compute return value addr.
  __ movq(Address(RSP, retval_offset), RAX);  // Set retval in NativeArguments.
  __ movq(RDI, RSP);  // Pass the pointer to the NativeArguments.
  __ call(RBX);

  // Reset exit frame information in Isolate structure.
  __ movq(Address(CTX, Isolate::top_exit_frame_info_offset()), Immediate(0));

  // Load Context pointer from Isolate structure into R8.
  __ movq(R8, Address(CTX, Isolate::top_context_offset()));

  // Reset Context pointer in Isolate structure.
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));
  __ movq(Address(CTX, Isolate::top_context_offset()), raw_null);

  // Cache Context pointer into CTX while executing Dart code.
  __ movq(CTX, R8);

  __ LeaveFrame();
  __ ret();
}


// Input parameters:
//   RBX: function object.
//   R10: arguments descriptor array (num_args is first Smi element).
void StubCode::GenerateCallStaticFunctionStub(Assembler* assembler) {
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));

  __ movq(RAX, FieldAddress(RBX, Function::code_offset()));
  __ cmpq(RAX, raw_null);
  Label function_compiled;
  __ j(NOT_EQUAL, &function_compiled, Assembler::kNearJump);

  // Create a stub frame as we are pushing some objects on the stack before
  // calling into the runtime.
  __ EnterFrame(0);

  __ pushq(R10);  // Preserve arguments descriptor array.
  __ pushq(RBX);
  __ CallRuntimeFromStub(kCompileFunctionRuntimeEntry);
  __ popq(RBX);  // Restore read-only function object argument in RBX.
  __ popq(R10);  // Restore arguments descriptor array.
  // Restore RAX.
  __ movq(RAX, FieldAddress(RBX, Function::code_offset()));

  // Remove the stub frame as we are about to jump to the dart function.
  __ LeaveFrame();

  __ Bind(&function_compiled);
  // Patch caller.
  __ EnterFrame(0);

  __ pushq(R10);  // Preserve arguments descriptor array.
  __ pushq(RBX);  // Preserve function object.
  __ CallRuntimeFromStub(kPatchStaticCallRuntimeEntry);
  __ popq(RBX);  // Restore function object argument in RBX.
  __ popq(R10);  // Restore arguments descriptor array.
  // Remove the stub frame as we are about to jump to the dart function.
  __ LeaveFrame();
  __ movq(RAX, FieldAddress(RBX, Function::code_offset()));

  __ movq(RBX, FieldAddress(RAX, Code::instructions_offset()));
  __ addq(RBX, Immediate(Instructions::HeaderSize() - kHeapObjectTag));
  __ jmp(RBX);
}


void StubCode::GenerateOptimizeInvokedFunctionStub(Assembler* assembler) {
  __ Unimplemented("OptimizeInvokedFunction stub");
}


void StubCode::GenerateFixCallersTargetStub(Assembler* assembler) {
  __ Unimplemented("FixCallersTarget stub");
}


// Lookup for [function-name, arg count] in 'functions_map_'.
// Input parameters (to be treated as read only, unless calling to target!):
//   RBX: ic-data array.
//   R10: arguments descriptor array (num_args is first Smi element).
//   Stack: return address, arguments.
// If the lookup succeeds we jump to the target method from here, otherwise
// we continue in code generated by the caller of 'MegamorphicLookup'.
static void MegamorphicLookup(Assembler* assembler) {
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));
  Label class_in_rax, smi_receiver, null_receiver, not_found;
  // Total number of args is the first Smi in args descriptor array (R10).
  __ movq(RAX, FieldAddress(R10, Array::data_offset()));
  __ movq(RAX, Address(RSP, RAX, TIMES_4, 0));  // Get receiver. RAX is a Smi.
  // TODO(srdjan): Remove the special casing below for null receiver, once
  // NullClass is implemented.
  __ cmpq(RAX, raw_null);
  // Use Object class if receiver is null.
  __ j(EQUAL, &null_receiver, Assembler::kNearJump);
  __ testq(RAX, Immediate(kSmiTagMask));
  __ j(ZERO, &smi_receiver, Assembler::kNearJump);
  __ movq(RAX, FieldAddress(RAX, Object::class_offset()));
  __ jmp(&class_in_rax, Assembler::kNearJump);
  __ Bind(&smi_receiver);
  // For Smis we need to get the class from the isolate.
  // Load current Isolate pointer from Context structure into RAX.
  __ movq(RAX, FieldAddress(CTX, Context::isolate_offset()));
  __ movq(RAX, Address(RAX, Isolate::object_store_offset()));
  __ movq(RAX, Address(RAX, ObjectStore::smi_class_offset()));
  __ jmp(&class_in_rax, Assembler::kNearJump);
  __ Bind(&null_receiver);
  __ movq(RAX, FieldAddress(CTX, Context::isolate_offset()));
  __ movq(RAX, Address(RAX, Isolate::object_store_offset()));
  __ movq(RAX, Address(RAX, ObjectStore::object_class_offset()));

  __ Bind(&class_in_rax);
  // Class is in RAX.

  Label loop, next_iteration;
  // Get functions_cache, since it is allocated lazily it maybe null.
  __ movq(RAX, FieldAddress(RAX, Class::functions_cache_offset()));
  // Iterate and search for identical name.
  __ leaq(R12, FieldAddress(RAX, Array::data_offset()));

  // R12 is  pointing into content of functions_map_ array.
  __ Bind(&loop);
  __ movq(R13, Address(R12, FunctionsCache::kFunctionName * kWordSize));

  __ cmpq(R13, raw_null);
  __ j(EQUAL, &not_found, Assembler::kNearJump);

  ASSERT(ICData::kNameIndex == 0);
  __ cmpq(R13, FieldAddress(RBX, Array::data_offset()));
  __ j(NOT_EQUAL, &next_iteration, Assembler::kNearJump);

  // Name found, check total argument count and named argument count.
  __ movq(RAX, FieldAddress(R10, Array::data_offset()));
  // RAX is total argument count as Smi.
  __ movq(R13, Address(R12, FunctionsCache::kArgCount * kWordSize));
  __ cmpq(RAX, R13);  // Compare total argument counts.
  __ j(NOT_EQUAL, &next_iteration, Assembler::kNearJump);
  __ subq(RAX, FieldAddress(R10, Array::data_offset() + kWordSize));
  // RAX is named argument count as Smi.
  __ movq(R13, Address(R12, FunctionsCache::kNamedArgCount * kWordSize));
  __ cmpq(RAX, R13);  // Compare named argument counts.
  __ j(NOT_EQUAL, &next_iteration, Assembler::kNearJump);

  // Argument count matches, jump to target.
  // R10: arguments descriptor array.
  __ movq(RBX, Address(R12, FunctionsCache::kFunction * kWordSize));
  __ movq(RBX, FieldAddress(RBX, Function::code_offset()));
  __ movq(RBX, FieldAddress(RBX, Code::instructions_offset()));
  __ addq(RBX, Immediate(Instructions::HeaderSize() - kHeapObjectTag));
  __ jmp(RBX);

  __ Bind(&next_iteration);
  __ AddImmediate(R12, Immediate(FunctionsCache::kNumEntries * kWordSize));
  __ jmp(&loop, Assembler::kNearJump);

  __ Bind(&not_found);
}


// Input parameters:
//   R13: argument count, may be zero.
// Uses RAX, RBX, R10, R12.
static void PushArgumentsArray(Assembler* assembler, intptr_t arg_offset) {
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));

  // Allocate array to store arguments of caller.
  __ movq(R10, R13);  // Arguments array length.
  __ SmiTag(R10);  // Convert to Smi.
  __ movq(RBX, raw_null);  // Null element type for raw Array.
  __ call(&StubCode::AllocateArrayLabel());
  __ SmiUntag(R10);
  // RAX: newly allocated array.
  // R10: length of the array (was preserved by the stub).
  __ pushq(RAX);  // Array is in RAX and on top of stack.
  __ leaq(R12, Address(RSP, R10, TIMES_8, arg_offset));  // Addr of first arg.
  __ leaq(RBX, FieldAddress(RAX, Array::data_offset()));
  Label loop, loop_condition;
  __ jmp(&loop_condition, Assembler::kNearJump);
  __ Bind(&loop);
  __ movq(RAX, Address(R12, 0));
  __ movq(Address(RBX, 0), RAX);
  __ AddImmediate(RBX, Immediate(kWordSize));
  __ AddImmediate(R12, Immediate(-kWordSize));
  __ Bind(&loop_condition);
  __ decq(R10);
  __ j(POSITIVE, &loop, Assembler::kNearJump);
}


// Input parameters:
//   RBX: ic-data array.
//   R10: arguments descriptor array (num_args is first Smi element).
// Note: The receiver object is the first argument to the function being
//       called, the stub accesses the receiver from this location directly
//       when trying to resolve the call.
// Uses R13.
void StubCode::GenerateMegamorphicLookupStub(Assembler* assembler) {
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));

  MegamorphicLookup(assembler);
  // Lookup in function_table_ failed, resolve, compile and enter function
  // into function_table_.

  // Create a stub frame as we are pushing some objects on the stack before
  // calling into the runtime.
  __ EnterFrame(0);

  // Preserve values across call to resolving.
  // Stack at this point:
  // TOS + 0: Saved RBP of previous frame. <== RBP
  // TOS + 1: Dart code return address
  // TOS + 2: Last argument of caller.
  // ....
  // Total number of args is the first Smi in args descriptor array (R10).
  __ movq(RAX, FieldAddress(R10, Array::data_offset()));
  __ movq(RAX, Address(RSP, RAX, TIMES_4, kWordSize));  // Get receiver.
  __ pushq(R10);  // Preserve arguments descriptor array.
  __ pushq(RAX);  // Preserve receiver.
  __ pushq(RBX);  // Preserve ic-data array.
  // First resolve the function to get the function object.

  __ pushq(raw_null);  // Setup space on stack for return value.
  __ pushq(RAX);  // Push receiver.
  __ CallRuntimeFromStub(kResolveCompileInstanceFunctionRuntimeEntry);
  __ popq(RAX);  // Remove receiver pushed earlier.
  __ popq(RBX);  // Pop returned code object into RBX.
  // Pop preserved values
  __ popq(R10);  // Restore ic-data array.
  __ popq(RAX);  // Restore receiver.
  __ popq(R13);  // Restore arguments descriptor array.

  __ cmpq(RBX, raw_null);
  Label check_implicit_closure;
  __ j(EQUAL, &check_implicit_closure, Assembler::kNearJump);

  // Remove the stub frame as we are about to jump to the dart function.
  __ LeaveFrame();

  __ movq(R10, R13);
  __ movq(RBX, FieldAddress(RBX, Code::instructions_offset()));
  __ addq(RBX, Immediate(Instructions::HeaderSize() - kHeapObjectTag));
  __ jmp(RBX);

  __ Bind(&check_implicit_closure);
  // RAX: receiver.
  // R10: ic-data array.
  // RBX: raw_null.
  // R13: arguments descriptor array.
  // The target function was not found.
  // First check to see if this is a getter function and we are
  // trying to create a closure of an instance function.
  // Push values that need to be preserved across runtime call.
  __ pushq(RAX);  // Preserve receiver.
  __ pushq(R10);  // Preserve ic-data array.
  __ pushq(R13);  // Preserve arguments descriptor array.

  __ pushq(raw_null);  // Setup space on stack for return value.
  __ pushq(RAX);  // Push receiver.
  __ pushq(R10);  // Ic-data array.
  __ CallRuntimeFromStub(kResolveImplicitClosureFunctionRuntimeEntry);
  __ popq(RAX);
  __ popq(RAX);
  __ popq(RBX);  // Get return value into RBX, might be Closure object.

  // Pop preserved values.
  __ popq(R13);  // Restore arguments descriptor array.
  __ popq(R10);  // Restore ic-data array.
  __ popq(RAX);  // Restore receiver.

  __ cmpq(RBX, raw_null);
  Label check_implicit_closure_through_getter;
  __ j(EQUAL, &check_implicit_closure_through_getter, Assembler::kNearJump);

  __ movq(RAX, RBX);  // Return value is the closure object.
  // Remove the stub frame as we are about return.
  __ LeaveFrame();
  __ ret();

  __ Bind(&check_implicit_closure_through_getter);
  // RAX: receiver.
  // R10: ic-data array.
  // RBX: raw_null.
  // R13: arguments descriptor array.
  // This is not the case of an instance so invoke the getter of the
  // same name and see if we get a closure back which we are then
  // supposed to invoke.
  // Push values that need to be preserved across runtime call.
  __ pushq(RAX);  // Preserve receiver.
  __ pushq(R10);  // Preserve ic-data array.
  __ pushq(R13);  // Preserve arguments descriptor array.

  __ pushq(raw_null);  // Setup space on stack for return value.
  __ pushq(RAX);  // Push receiver.
  __ pushq(R10);  // Ic-data array.
  __ CallRuntimeFromStub(kResolveImplicitClosureThroughGetterRuntimeEntry);
  __ popq(R10);  // Pop argument.
  __ popq(RAX);  // Pop argument.
  __ popq(RBX);  // get return value into RBX, might be Closure object.

  // Pop preserved values.
  __ popq(R13);  // Restore arguments descriptor array.
  __ popq(R10);  // Restore ic-data array.
  __ popq(RAX);  // Restore receiver.

  __ cmpq(RBX, raw_null);
  Label function_not_found;
  __ j(EQUAL, &function_not_found);

  // RBX: Closure object.
  // R13: Arguments descriptor array.
  __ pushq(raw_null);  // Setup space on stack for result from invoking Closure.
  __ pushq(RBX);  // Closure object.
  __ pushq(R13);  // Arguments descriptor.
  __ movq(R13, FieldAddress(R13, Array::data_offset()));
  __ SmiUntag(R13);
  __ subq(R13, Immediate(1));  // Arguments array length, minus the receiver.
  PushArgumentsArray(assembler, (kWordSize * 5));
  // Stack layout explaining "(kWordSize * 5)" offset.
  // TOS + 0: Argument array.
  // TOS + 1: Arguments descriptor array.
  // TOS + 2: Closure object.
  // TOS + 3: Place for result from closure function.
  // TOS + 4: Saved RBP of previous frame. <== RBP
  // TOS + 5: Dart code return address
  // TOS + 6: Last argument of caller.
  // ....

  __ CallRuntimeFromStub(kInvokeImplicitClosureFunctionRuntimeEntry);
  // Remove arguments.
  __ popq(RAX);
  __ popq(RAX);
  __ popq(RAX);
  __ popq(RAX);  // Get result into RAX.

  // Remove the stub frame as we are about to return.
  __ LeaveFrame();
  __ ret();

  __ Bind(&function_not_found);
  // The target function was not found, so invoke method
  // "void noSuchMethod(function_name, args_array)".
  //   RAX: receiver.
  //   R10: ic-data array.
  //   RBX: raw_null.
  //   R13: argument descriptor array.

  __ pushq(raw_null);  // Setup space on stack for result from noSuchMethod.
  __ pushq(RAX);  // Receiver.
  __ pushq(R10);  // IC-data array.
  __ pushq(R13);  // Argument descriptor array.
  __ movq(R13, FieldAddress(R13, Array::data_offset()));
  __ SmiUntag(R13);
  __ subq(R13, Immediate(1));  // Arguments array length, minus the receiver.
  // See stack layout below explaining "wordSize * 6" offset.
  PushArgumentsArray(assembler, (kWordSize * 6));

  // Stack:
  // TOS + 0: Argument array.
  // TOS + 1: Argument descriptor array.
  // TOS + 2: IC-data array.
  // TOS + 3: Receiver.
  // TOS + 4: Place for result from noSuchMethod.
  // TOS + 5: Saved RBP of previous frame. <== RBP
  // TOS + 6: Dart code return address
  // TOS + 7: Last argument of caller.
  // ....

  __ CallRuntimeFromStub(kInvokeNoSuchMethodFunctionRuntimeEntry);
  // Remove arguments.
  __ popq(RAX);
  __ popq(RAX);
  __ popq(RAX);
  __ popq(RAX);
  __ popq(RAX);  // Get result into RAX.

  // Remove the stub frame as we are about to return.
  __ LeaveFrame();
  __ ret();
}


void StubCode::GenerateDeoptimizeStub(Assembler* assembler) {
  __ Unimplemented("Deoptimize stub");
}


// Called for inline allocation of arrays.
// Input parameters:
//   R10 : Array length as Smi.
//   RBX : array element type (either NULL or an instantiated type).
// NOTE: R10 cannot be clobbered here as the caller relies on it being saved.
// The newly allocated object is returned in RAX.
void StubCode::GenerateAllocateArrayStub(Assembler* assembler) {
  Label slow_case;
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));

  if (FLAG_inline_alloc) {
    // Compute the size to be allocated, it is based on the array length
    // and it computed as:
    // RoundedAllocationSize((array_length * kwordSize) + sizeof(RawArray)).
    // Assert that length is a Smi.
    __ testq(R10, Immediate(kSmiTagSize));
    if (FLAG_use_slow_path) {
      __ jmp(&slow_case);
    } else {
      __ j(NOT_ZERO, &slow_case);
    }
    __ movq(R13, FieldAddress(CTX, Context::isolate_offset()));
    __ movq(R13, Address(R13, Isolate::heap_offset()));
    __ movq(R13, Address(R13, Heap::new_space_offset()));

    // Calculate and align allocation size.
    // Load new object start and calculate next object start.
    // RBX: array element type.
    // R10: Array length as Smi.
    // R13: Points to new space object.
    __ movq(RAX, Address(R13, Scavenger::top_offset()));
    intptr_t fixed_size = sizeof(RawArray) + kObjectAlignment - 1;
    __ leaq(R12, Address(R10, TIMES_4, fixed_size));  // R10 is Smi.
    ASSERT(kSmiTagShift == 1);
    __ andq(R12, Immediate(-kObjectAlignment));
    __ leaq(R12, Address(RAX, R12, TIMES_1, 0));

    // Check if the allocation fits into the remaining space.
    // RAX: potential new object start.
    // R12: potential next object start.
    // RBX: array element type.
    // R10: Array length as Smi.
    // R13: Points to new space object.
    __ cmpq(R12, Address(R13, Scavenger::end_offset()));
    __ j(ABOVE_EQUAL, &slow_case);

    // Successfully allocated the object(s), now update top to point to
    // next object start and initialize the object.
    // RAX: potential new object start.
    // R12: potential next object start.
    // R13: Points to new space object.
    __ movq(Address(R13, Scavenger::top_offset()), R12);
    __ addq(RAX, Immediate(kHeapObjectTag));

    // RAX: new object start as a tagged pointer.
    // R12: new object end address.
    // RBX: array element type.
    // R10: Array length as Smi.

    // Store the type argument field.
    __ StoreIntoObject(RAX,
                       FieldAddress(RAX, Array::type_arguments_offset()),
                       RBX);

    // Set the length field.
    __ StoreIntoObject(RAX, FieldAddress(RAX, Array::length_offset()), R10);

    // Store class value for array.
    __ movq(RBX, FieldAddress(CTX, Context::isolate_offset()));
    __ movq(RBX, Address(RBX, Isolate::object_store_offset()));
    __ movq(RBX, Address(RBX, ObjectStore::array_class_offset()));
    __ StoreIntoObject(RAX, FieldAddress(RAX, Array::class_offset()), RBX);
    // Calculate the size tag.
    // RAX: new object start as a tagged pointer.
    // R12: new object end address.
    // R10: Array length as Smi.
    {
      Label size_tag_overflow, done;
      __ leaq(RBX, Address(R10, TIMES_2, fixed_size));  // R10 is Smi.
      ASSERT(kSmiTagShift == 1);
      __ andq(RBX, Immediate(-kObjectAlignment));
      __ cmpq(RBX, Immediate(RawObject::SizeTag::kMaxSizeTag));
      __ j(ABOVE, &size_tag_overflow, Assembler::kNearJump);
      __ shlq(RBX, Immediate(RawObject::kSizeTagBit - kObjectAlignmentLog2));
      __ movq(FieldAddress(RAX, Array::tags_offset()), RBX);
      __ jmp(&done);

      __ Bind(&size_tag_overflow);
      __ movq(FieldAddress(RAX, Array::tags_offset()), Immediate(0));
      __ Bind(&done);
    }

    // Initialize all array elements to raw_null.
    // RAX: new object start as a tagged pointer.
    // R12: new object end address.
    // RBX: iterator which initially points to the start of the variable
    // data area to be initialized.
    __ leaq(RBX, FieldAddress(RAX, Array::data_offset()));
    Label done;
    Label init_loop;
    __ Bind(&init_loop);
    __ cmpq(RBX, R12);
    __ j(ABOVE_EQUAL, &done, Assembler::kNearJump);
    __ movq(Address(RBX, 0), raw_null);
    __ addq(RBX, Immediate(kWordSize));
    __ jmp(&init_loop, Assembler::kNearJump);
    __ Bind(&done);

    // Done allocating and initializing the array.
    // RAX: new object.
    // R10: Array length as Smi (preserved for the caller.)
    __ ret();
  }

  // Unable to allocate the array using the fast inline code, just call
  // into the runtime.
  __ Bind(&slow_case);
  __ EnterFrame(0);
  __ pushq(raw_null);  // Setup space on stack for return value.
  __ pushq(R10);  // Array length as Smi.
  __ pushq(RBX);  // Element type.
  __ pushq(raw_null);  // Null instantiator.
  __ CallRuntimeFromStub(kAllocateArrayRuntimeEntry);
  __ popq(RAX);  // Pop instantiator.
  __ popq(RAX);  // Pop element type argument.
  __ popq(R10);  // Pop array length argument.
  __ popq(RAX);  // Pop return value from return slot.
  __ LeaveFrame();
  __ ret();
}


void StubCode::GenerateCallClosureFunctionStub(Assembler* assembler) {
  __ Unimplemented("CallClosureFunction stub");
}


// Called when invoking Dart code from C++ (VM code).
// Input parameters:
//   RSP : points to return address.
//   RDI : entrypoint of the Dart function to call.
//   RSI : arguments descriptor array.
//   RDX : pointer to the argument array.
//   RCX : new context containing the current isolate pointer.
void StubCode::GenerateInvokeDartCodeStub(Assembler* assembler) {
  // Save frame pointer coming in.
  __ EnterFrame(0);

  // Save arguments descriptor array and new context.
  const intptr_t kArgumentsDescOffset = -1 * kWordSize;
  __ pushq(RSI);
  const intptr_t kNewContextOffset = -2 * kWordSize;
  __ pushq(RCX);

  // Save C++ ABI callee-saved registers.
  __ pushq(RBX);
  __ pushq(R12);
  __ pushq(R13);
  __ pushq(R14);
  __ pushq(R15);

  // The new Context structure contains a pointer to the current Isolate
  // structure. Cache the Context pointer in the CTX register so that it is
  // available in generated code and calls to Isolate::Current() need not be
  // done. The assumption is that this register will never be clobbered by
  // compiled or runtime stub code.

  // Cache the new Context pointer into CTX while executing Dart code.
  __ movq(CTX, Address(RCX, VMHandles::kOffsetOfRawPtrInHandle));

  // Load Isolate pointer from Context structure into R8.
  __ movq(R8, FieldAddress(CTX, Context::isolate_offset()));

  // Save the top exit frame info. Use RAX as a temporary register.
  __ movq(RAX, Address(R8, Isolate::top_exit_frame_info_offset()));
  __ pushq(RAX);
  __ movq(Address(R8, Isolate::top_exit_frame_info_offset()), Immediate(0));

  // StackFrameIterator reads the top exit frame info saved in this frame.
  // The constant kExitLinkOffsetInEntryFrame must be kept in sync with the
  // code above: kExitLinkOffsetInEntryFrame = -8 * kWordSize.

  // Save the old Context pointer. Use RAX as a temporary register.
  // Note that VisitObjectPointers will find this saved Context pointer during
  // GC marking, since it traverses any information between SP and
  // FP - kExitLinkOffsetInEntryFrame.
  __ movq(RAX, Address(R8, Isolate::top_context_offset()));
  __ pushq(RAX);

  // Load arguments descriptor array into R10, which is passed to Dart code.
  __ movq(R10, Address(RSI, VMHandles::kOffsetOfRawPtrInHandle));

  // Load number of arguments into RBX.
  __ movq(RBX, FieldAddress(R10, Array::data_offset()));
  __ SmiUntag(RBX);

  // Set up arguments for the Dart call.
  Label push_arguments;
  Label done_push_arguments;
  __ testq(RBX, RBX);  // check if there are arguments.
  __ j(ZERO, &done_push_arguments, Assembler::kNearJump);
  __ movq(RAX, Immediate(0));
  __ Bind(&push_arguments);
  __ movq(RCX, Address(RDX, RAX, TIMES_8, 0));  // RDX is start of arguments.
  __ movq(RCX, Address(RCX, VMHandles::kOffsetOfRawPtrInHandle));
  __ pushq(RCX);
  __ incq(RAX);
  __ cmpq(RAX, RBX);
  __ j(LESS, &push_arguments, Assembler::kNearJump);
  __ Bind(&done_push_arguments);

  // Call the Dart code entrypoint.
  __ call(RDI);  // R10 is the arguments descriptor array.

  // Read the saved new Context pointer.
  __ movq(CTX, Address(RBP, kNewContextOffset));
  __ movq(CTX, Address(CTX, VMHandles::kOffsetOfRawPtrInHandle));

  // Read the saved arguments descriptor array to obtain the number of passed
  // arguments, which is the first element of the array, a Smi.
  __ movq(RSI, Address(RBP, kArgumentsDescOffset));
  __ movq(R10, Address(RSI, VMHandles::kOffsetOfRawPtrInHandle));
  __ movq(RDX, FieldAddress(R10, Array::data_offset()));
  // Get rid of arguments pushed on the stack.
  __ leaq(RSP, Address(RSP, RDX, TIMES_4, 0));  // RDX is a Smi.

  // Load Isolate pointer from Context structure into CTX. Drop Context.
  __ movq(CTX, FieldAddress(CTX, Context::isolate_offset()));

  // Restore the saved Context pointer into the Isolate structure.
  // Uses RCX as a temporary register for this.
  __ popq(RCX);
  __ movq(Address(CTX, Isolate::top_context_offset()), RCX);

  // Restore the saved top exit frame info back into the Isolate structure.
  // Uses RDX as a temporary register for this.
  __ popq(RDX);
  __ movq(Address(CTX, Isolate::top_exit_frame_info_offset()), RDX);

  // Restore C++ ABI callee-saved registers.
  __ popq(R15);
  __ popq(R14);
  __ popq(R13);
  __ popq(R12);
  __ popq(RBX);

  // Restore the frame pointer.
  __ LeaveFrame();

  __ ret();
}


// Called for inline allocation of contexts.
// Input:
// RDX: number of context variables.
// Output:
// RAX: new allocated RawContext object.
// RBX and RDX are destroyed.
void StubCode::GenerateAllocateContextStub(Assembler* assembler) {
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));
  if (false) {
    // TODO(regis): Implement fast inline allocation of contexts.
    __ Unimplemented("AllocateContext stub - inline allocation");
  }
  // Create the stub frame.
  __ EnterFrame(0);
  __ pushq(raw_null);  // Setup space on stack for the return value.
  __ SmiTag(RDX);
  __ pushq(RDX);  // Push number of context variables.
  __ CallRuntimeFromStub(kAllocateContextRuntimeEntry);  // Allocate context.
  __ popq(RAX);  // Pop number of context variables argument.
  __ popq(RAX);  // Pop the new context object.
  // RAX: new object
  // Restore the frame pointer.
  __ LeaveFrame();
  __ ret();
}




// Called for inline allocation of objects.
// Input parameters:
//   RSP + 16 : type arguments object (only if class is parameterized).
//   RSP + 8 : type arguments of instantiator (only if class is parameterized).
//   RSP : points to return address.
// Uses RAX, RBX, RCX, RDX, RDI as temporary registers.
void StubCode::GenerateAllocationStubForClass(Assembler* assembler,
                                              const Class& cls) {
  const intptr_t kObjectTypeArgumentsOffset = 2 * kWordSize;
  const intptr_t kInstantiatorTypeArgumentsOffset = 1 * kWordSize;
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));
  // The generated code is different if the class is parameterized.
  const bool is_cls_parameterized =
      cls.type_arguments_instance_field_offset() != Class::kNoTypeArguments;
  // kInlineInstanceSize is a constant used as a threshold for determining
  // when the object initialization should be done as a loop or as
  // straight line code.
  const int kInlineInstanceSize = 12;  // In words.
  const intptr_t instance_size = cls.instance_size();
  ASSERT(instance_size > 0);
  const intptr_t type_args_size = InstantiatedTypeArguments::InstanceSize();
  if (FLAG_inline_alloc &&
      PageSpace::IsPageAllocatableSize(instance_size + type_args_size)) {
    Label slow_case;
    Heap* heap = Isolate::Current()->heap();
    __ movq(RAX, Immediate(heap->TopAddress()));
    __ movq(RAX, Address(RAX, 0));
    __ leaq(RBX, Address(RAX, instance_size));
    if (is_cls_parameterized) {
      __ movq(RCX, RBX);
      // A new InstantiatedTypeArguments object only needs to be allocated if
      // the instantiator is non-null.
      Label null_instantiator;
      __ cmpq(Address(RSP, kInstantiatorTypeArgumentsOffset), raw_null);
      __ j(EQUAL, &null_instantiator, Assembler::kNearJump);
      __ addq(RBX, Immediate(type_args_size));
      __ Bind(&null_instantiator);
      // RCX: potential new object end and, if RCX != RBX, potential new
      // InstantiatedTypeArguments object start.
    }
    // Check if the allocation fits into the remaining space.
    // RAX: potential new object start.
    // RBX: potential next object start.
    __ movq(RDI, Immediate(heap->EndAddress()));
    __ cmpq(RBX, Address(RDI, 0));
    if (FLAG_use_slow_path) {
      __ jmp(&slow_case);
    } else {
      __ j(ABOVE_EQUAL, &slow_case);
    }

    // Successfully allocated the object(s), now update top to point to
    // next object start and initialize the object.
    __ movq(RDI, Immediate(heap->TopAddress()));
    __ movq(Address(RDI, 0), RBX);

    if (is_cls_parameterized) {
      // Initialize the type arguments field in the object.
      // RAX: new object start.
      // RCX: potential new object end and, if RCX != RBX, potential new
      // InstantiatedTypeArguments object start.
      // RBX: next object start.
      Label type_arguments_ready;
      __ movq(RDI, Address(RSP, kObjectTypeArgumentsOffset));
      __ cmpq(RCX, RBX);
      __ j(EQUAL, &type_arguments_ready, Assembler::kNearJump);
      // Initialize InstantiatedTypeArguments object at RCX.
      __ movq(Address(RCX,
          InstantiatedTypeArguments::uninstantiated_type_arguments_offset()),
              RDI);
      __ movq(RDX, Address(RSP, kInstantiatorTypeArgumentsOffset));
      __ movq(Address(RCX,
          InstantiatedTypeArguments::instantiator_type_arguments_offset()),
              RDX);
      __ LoadObject(RDX,
          Class::ZoneHandle(Object::instantiated_type_arguments_class()));
      __ movq(Address(RCX, Instance::class_offset()), RDX);  // Set its class.
      // Set the tags.
      __ movq(Address(RCX, Instance::tags_offset()),
              Immediate(RawObject::SizeTag::encode(type_args_size)));
      // Set the new InstantiatedTypeArguments object (RCX) as the type
      // arguments (RDI) of the new object (RAX).
      __ movq(RDI, RCX);
      __ addq(RDI, Immediate(kHeapObjectTag));
      // Set RBX to new object end.
      __ movq(RBX, RCX);
      __ Bind(&type_arguments_ready);
      // RAX: new object.
      // RDI: new object type arguments.
    }

    // Initialize the class field in the object.
    // RAX: new object start.
    // RBX: next object start.
    // RDI: new object type arguments (if is_cls_parameterized).
    __ LoadObject(RDX, cls);  // Load class of object to be allocated.
    __ movq(Address(RAX, Instance::class_offset()), RDX);
    // Set the tags.
    __ movq(Address(RAX, Instance::tags_offset()),
            Immediate(RawObject::SizeTag::encode(instance_size)));

    // Initialize the remaining words of the object.
    const Immediate raw_null =
        Immediate(reinterpret_cast<intptr_t>(Object::null()));

    // RAX: new object start.
    // RBX: next object start.
    // RDX: class of the object to be allocated.
    // First try inlining the initialization without a loop.
    if (instance_size < (kInlineInstanceSize * kWordSize) &&
        cls.num_native_fields() == 0) {
      // Check if the object contains any non-header fields.
      // Small objects are initialized using a consecutive set of writes.
      for (intptr_t current_offset = sizeof(RawObject);
           current_offset < instance_size;
           current_offset += kWordSize) {
        __ movq(Address(RAX, current_offset), raw_null);
      }
    } else {
      __ leaq(RCX, Address(RAX, sizeof(RawObject)));
      // Loop until the whole object is initialized.
      Label init_loop;
      if (cls.num_native_fields() > 0) {
        // Initialize native fields.
        // RAX: new object.
        // RBX: next object start.
        // RDX: class of the object to be allocated.
        // RCX: next word to be initialized.
        intptr_t offset = Class::num_native_fields_offset() - kHeapObjectTag;
        __ movq(RDX, Address(RDX, offset));
        __ leaq(RDX, Address(RAX, RDX, TIMES_8, sizeof(RawObject)));

        // RDX: start of dart fields.
        // RCX: next word to be initialized.
        Label init_native_loop;
        __ Bind(&init_native_loop);
        __ cmpq(RCX, RDX);
        __ j(ABOVE_EQUAL, &init_loop, Assembler::kNearJump);
        __ movq(Address(RCX, 0), Immediate(0));
        __ addq(RCX, Immediate(kWordSize));
        __ jmp(&init_native_loop, Assembler::kNearJump);
      }
      // Now initialize the dart fields.
      // RAX: new object.
      // RBX: next object start.
      // RCX: next word to be initialized.
      Label done;
      __ Bind(&init_loop);
      __ cmpq(RCX, RBX);
      __ j(ABOVE_EQUAL, &done, Assembler::kNearJump);
      __ movq(Address(RCX, 0), raw_null);
      __ addq(RCX, Immediate(kWordSize));
      __ jmp(&init_loop, Assembler::kNearJump);
      __ Bind(&done);
    }
    if (is_cls_parameterized) {
      // RDI: new object type arguments.
      // Set the type arguments in the new object.
      __ movq(Address(RAX, cls.type_arguments_instance_field_offset()), RDI);
    }
    // Done allocating and initializing the instance.
    // RAX: new object.
    __ addq(RAX, Immediate(kHeapObjectTag));
    __ ret();

    __ Bind(&slow_case);
  }
  if (is_cls_parameterized) {
    __ movq(RAX, Address(RSP, kObjectTypeArgumentsOffset));
    __ movq(RDX, Address(RSP, kInstantiatorTypeArgumentsOffset));
  }
  // Create a stub frame.
  __ EnterFrame(0);
  __ pushq(raw_null);  // Setup space on stack for return value.
  __ PushObject(cls);  // Push class of object to be allocated.
  if (is_cls_parameterized) {
    __ pushq(RAX);  // Push type arguments of object to be allocated.
    __ pushq(RDX);  // Push type arguments of instantiator.
  } else {
    __ pushq(raw_null);  // Push null type arguments.
    __ pushq(raw_null);  // Push null instantiator.
  }
  __ CallRuntimeFromStub(kAllocateObjectRuntimeEntry);  // Allocate object.
  __ popq(RAX);  // Pop argument (instantiator).
  __ popq(RAX);  // Pop argument (type arguments of object).
  __ popq(RAX);  // Pop argument (class of object).
  __ popq(RAX);  // Pop result (newly allocated object).
  // RAX: new object
  // Restore the frame pointer.
  __ LeaveFrame();
  __ ret();
}


// Called for inline allocation of closures.
// Input parameters:
//   If the signature class is not parameterized, the receiver, if any, will be
//   at RSP + 8 instead of RSP + 16, since no type arguments are passed.
//   RSP + 16 (or RSP + 8): receiver (only if implicit instance closure).
//   RSP + 8 : type arguments object (only if signature class is parameterized).
//   RSP : points to return address.
// Uses RAX, RCX as temporary registers.
void StubCode::GenerateAllocationStubForClosure(Assembler* assembler,
                                                const Function& func) {
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));
  ASSERT(func.IsClosureFunction());
  const bool is_implicit_static_closure =
      func.IsImplicitStaticClosureFunction();
  const bool is_implicit_instance_closure =
      func.IsImplicitInstanceClosureFunction();
  const Class& cls = Class::ZoneHandle(func.signature_class());
  const bool has_type_arguments = cls.HasTypeArguments();
  const intptr_t kTypeArgumentsOffset = 1 * kWordSize;
  const intptr_t kReceiverOffset = (has_type_arguments ? 2 : 1) * kWordSize;
  if (false) {
    // TODO(regis): Implement inline context allocation.
    __ Unimplemented("AllocateClosure stub - inline allocation");
  }
  if (has_type_arguments) {
    __ movq(RCX, Address(RSP, kTypeArgumentsOffset));
  }
  if (is_implicit_instance_closure) {
    __ movq(RAX, Address(RSP, kReceiverOffset));
  }
  // Create the stub frame.
  __ EnterFrame(0);
  __ pushq(raw_null);  // Setup space on stack for the return value.
  __ PushObject(func);
  if (is_implicit_static_closure) {
    __ CallRuntimeFromStub(kAllocateImplicitStaticClosureRuntimeEntry);
  } else {
    if (is_implicit_instance_closure) {
      __ pushq(RAX);  // Receiver.
    }
    if (has_type_arguments) {
      __ pushq(RCX);  // Push type arguments of closure to be allocated.
    } else {
      __ pushq(raw_null);  // Push null type arguments.
    }
    if (is_implicit_instance_closure) {
      __ CallRuntimeFromStub(kAllocateImplicitInstanceClosureRuntimeEntry);
      __ popq(RAX);  // Pop type arguments.
      __ popq(RAX);  // Pop receiver.
    } else {
      ASSERT(func.IsNonImplicitClosureFunction());
      __ CallRuntimeFromStub(kAllocateClosureRuntimeEntry);
      __ popq(RAX);  // Pop type arguments.
    }
  }
  __ popq(RAX);  // Pop the function object.
  __ popq(RAX);  // Pop the result.
  // RAX: New closure object.
  // Restore the calling frame.
  __ LeaveFrame();
  __ ret();
}


void StubCode::GenerateCallNoSuchMethodFunctionStub(Assembler* assembler) {
  __ Unimplemented("CallNoSuchMethodFunction stub");
}


// Generate inline cache check for 'num_args'.
//  RBX: Inline cache data array.
//  R10: Arguments array.
//  TOS(0): return address
// Control flow:
// - If receiver is null -> jump to IC miss.
// - If receiver is Smi -> load Smi class.
// - If receiver is not-Smi -> load receiver's class.
// - Check if 'num_args' (including receiver) match any IC data group.
// - Match found -> jump to target.
// - Match not found -> jump to IC miss.
void StubCode::GenerateNArgsCheckInlineCacheStub(Assembler* assembler,
                                                 intptr_t num_args) {
  ASSERT(num_args > 0);
  // Get receiver.
  __ movq(RAX, FieldAddress(R10, Array::data_offset()));
  __ movq(RAX, Address(RSP, RAX, TIMES_4, 0));  // RAX is Smi.

  Label get_class, ic_miss;
  __ call(&get_class);
  // RAX: receiver's class
  // RBX: IC data array.

#if defined(DEBUG)
  { Label ok;
    // Check that the IC data array has NumberOfArgumentsChecked() == num_args.
    __ movq(RCX, FieldAddress(RBX,
        Array::data_offset() + ICData::kNumArgsCheckedIndex * kWordSize));
    const Immediate value =
        Immediate(reinterpret_cast<int64_t>(Smi::New(num_args)));
    __ cmpq(RCX, value);
    __ j(EQUAL, &ok, Assembler::kNearJump);
    __ Stop("Incorrect stub for IC data");
    __ Bind(&ok);
  }
#endif  // DEBUG

  // Loop that checks if there is an IC data match.
  // RAX: receiver's class.
  // RBX: IC data array (preserved).
  __ leaq(R12, FieldAddress(RBX,
      Array::data_offset() + ICData::kChecksStartIndex * kWordSize));
  // R12: pointing to a class to check against (into IC data array).
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));
  Label loop, found;
  if (num_args == 1) {
    __ Bind(&loop);
    __ movq(R13, Address(R12, 0));  // Get class to check.
    __ cmpq(RAX, R13);  // Match?
    __ j(EQUAL, &found, Assembler::kNearJump);
    __ addq(R12, Immediate(kWordSize * 2));  // Next element (class + target).
    __ cmpq(R13, raw_null);   // Done?
    __ j(NOT_EQUAL, &loop, Assembler::kNearJump);
  } else if (num_args == 2) {
    Label no_match;
    __ Bind(&loop);
    __ movq(R13, Address(R12, 0));  // Get class from IC data to check.
    // Get receiver.
    __ movq(RAX, FieldAddress(R10, Array::data_offset()));
    __ movq(RAX, Address(RSP, RAX, TIMES_4, 0));  // RAX is Smi.
    __ call(&get_class);
    __ cmpq(RAX, R13);  // Match?
    __ j(NOT_EQUAL, &no_match, Assembler::kNearJump);
    // Check second.
    __ movq(R13, Address(R12, kWordSize));  // Get class from IC data to check.
    // Get next argument.
    __ movq(RAX, FieldAddress(R10, Array::data_offset()));
    __ movq(RAX, Address(RSP, RAX, TIMES_4, -kWordSize));  // RAX is Smi.
    __ call(&get_class);
    __ cmpq(RAX, R13);  // Match?
    __ j(EQUAL, &found);
    __ Bind(&no_match);
    __ addq(R12, Immediate(kWordSize * (1 + num_args)));  // Next element.
    __ cmpq(R13, raw_null);   // Done?
    __ j(NOT_EQUAL, &loop, Assembler::kNearJump);
  }

  __ Bind(&ic_miss);
  // Get receiver, again.
  __ movq(RAX, FieldAddress(R10, Array::data_offset()));
  __ leaq(RAX, Address(RSP, RAX, TIMES_4, 0));  // RAX is Smi.
  __ EnterFrame(0);
  __ pushq(R10);  // Preserve arguments array.
  __ pushq(RBX);  // Preserve IC data array
  __ pushq(raw_null);  // Setup space on stack for result (target code object).
  __ movq(R10, FieldAddress(R10, Array::data_offset()));
  // Push call arguments.
  for (intptr_t i = 0; i < num_args; i++) {
    __ movq(R10, Address(RAX, -kWordSize * i));
    __ pushq(R10);
  }
  if (num_args == 1) {
    __ CallRuntimeFromStub(kInlineCacheMissHandlerOneArgRuntimeEntry);
  } else if (num_args == 2) {
    __ CallRuntimeFromStub(kInlineCacheMissHandlerTwoArgsRuntimeEntry);
  } else {
    UNIMPLEMENTED();
  }
  // Remove call arguments pushed earlier.
  for (intptr_t i = 0; i < num_args; i++) {
    __ popq(RAX);
  }
  __ popq(RAX);  // Pop returned code object into RAX (null if not found).
  __ popq(RBX);  // Restore IC data array.
  __ popq(R10);  // Restore arguments array.
  __ LeaveFrame();
  Label call_target_function;
  __ cmpq(RAX, raw_null);
  __ j(NOT_EQUAL, &call_target_function, Assembler::kNearJump);
  // NoSuchMethod or closure.
  __ jmp(&StubCode::MegamorphicLookupLabel());

  __ Bind(&found);
  // R12: Pointer to an IC data check group (classes + target)
  __ movq(RAX, Address(R12, kWordSize * num_args));  // Target function.

  __ Bind(&call_target_function);
  // RAX: Target function.
  __ movq(RAX, FieldAddress(RAX, Function::code_offset()));
  __ movq(RAX, FieldAddress(RAX, Code::instructions_offset()));
  __ addq(RAX, Immediate(Instructions::HeaderSize() - kHeapObjectTag));
  __ jmp(RAX);

  __ Bind(&get_class);
  Label not_smi;
  // Test if Smi -> load Smi class for comparison.
  __ testq(RAX, Immediate(kSmiTagMask));
  __ j(NOT_ZERO, &not_smi, Assembler::kNearJump);
  const Class& smi_class =
      Class::ZoneHandle(Isolate::Current()->object_store()->smi_class());
  __ LoadObject(RAX, smi_class);
  __ ret();

  __ Bind(&not_smi);
  __ movq(RAX, FieldAddress(RAX, Object::class_offset()));
  __ ret();
}


// Use inline cache data array to invoke the target or continue in inline
// cache miss handler. Stub for 1-argument check (receiver class).
//  RCX: Inline cache data array
//  RDX: Arguments array
//  TOS(0): return address
// Inline cache data array structure:
// 0: function-name
// 1: N, number of arguments checked.
// 2 .. (length - 1): group of checks, each check containing:
//   - N classes.
//   - 1 target function.
void StubCode::GenerateOneArgCheckInlineCacheStub(Assembler* assembler) {
  return GenerateNArgsCheckInlineCacheStub(assembler, 1);
}


void StubCode::GenerateTwoArgsCheckInlineCacheStub(Assembler* assembler) {
  return GenerateNArgsCheckInlineCacheStub(assembler, 2);
}


void StubCode::GenerateBreakpointStaticStub(Assembler* assembler) {
  __ Unimplemented("BreakpointStatic stub");
}


void StubCode::GenerateBreakpointDynamicStub(Assembler* assembler) {
  __ Unimplemented("BreakpointDynamic stub");
}

}  // namespace dart

#endif  // defined TARGET_ARCH_X64
