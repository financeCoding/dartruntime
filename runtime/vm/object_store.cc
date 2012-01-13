// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/object_store.h"

#include "vm/exceptions.h"
#include "vm/isolate.h"
#include "vm/longjump.h"
#include "vm/object.h"
#include "vm/raw_object.h"
#include "vm/visitor.h"

namespace dart {

ObjectStore::ObjectStore()
  : object_class_(Class::null()),
    function_interface_(Type::null()),
    number_interface_(Type::null()),
    int_interface_(Type::null()),
    smi_class_(Class::null()),
    mint_class_(Class::null()),
    bigint_class_(Class::null()),
    double_interface_(Type::null()),
    double_class_(Class::null()),
    string_interface_(Type::null()),
    one_byte_string_class_(Class::null()),
    two_byte_string_class_(Class::null()),
    four_byte_string_class_(Class::null()),
    external_one_byte_string_class_(Class::null()),
    external_two_byte_string_class_(Class::null()),
    external_four_byte_string_class_(Class::null()),
    bool_interface_(Type::null()),
    bool_class_(Class::null()),
    list_interface_(Type::null()),
    array_class_(Class::null()),
    immutable_array_class_(Class::null()),
    byte_buffer_class_(Class::null()),
    stacktrace_class_(Class::null()),
    jsregexp_class_(Class::null()),
    true_value_(Bool::null()),
    false_value_(Bool::null()),
    empty_array_(Array::null()),
    symbol_table_(Array::null()),
    canonical_type_arguments_(Array::null()),
    core_library_(Library::null()),
    core_impl_library_(Library::null()),
    native_wrappers_library_(Library::null()),
    root_library_(Library::null()),
    registered_libraries_(Library::null()),
    pending_classes_(Array::null()),
    sticky_error_(String::null()),
    empty_context_(Context::null()),
    stack_overflow_(Instance::null()),
    out_of_memory_(Instance::null()),
    preallocate_objects_called_(false) {
}


ObjectStore::~ObjectStore() {
}


void ObjectStore::VisitObjectPointers(ObjectPointerVisitor* visitor) {
  ASSERT(visitor != NULL);
  visitor->VisitPointers(from(), to());
}


void ObjectStore::Init(Isolate* isolate) {
  ASSERT(isolate->object_store() == NULL);
  ObjectStore* store = new ObjectStore();
  isolate->set_object_store(store);
}


bool ObjectStore::PreallocateObjects() {
  if (preallocate_objects_called_) {
    return true;
  }

  Isolate* isolate = Isolate::Current();
  ASSERT(isolate != NULL && isolate->object_store() == this);
  LongJump* base = isolate->long_jump_base();
  LongJump jump;
  isolate->set_long_jump_base(&jump);
  if (setjmp(*jump.Set()) == 0) {
    GrowableArray<const Object*> args;
    Instance& exception =Instance::Handle();
    exception = Exceptions::Create(Exceptions::kStackOverflow, args);
    set_stack_overflow(exception);
    exception = Exceptions::Create(Exceptions::kOutOfMemory, args);
    set_out_of_memory(exception);
  } else {
    return false;
  }
  isolate->set_long_jump_base(base);
  preallocate_objects_called_ = true;
  return true;
}


RawClass* ObjectStore::GetClass(int index) {
  switch (index) {
    case kObjectClass: return object_class_;
    case kSmiClass: return smi_class_;
    case kMintClass: return mint_class_;
    case kBigintClass: return bigint_class_;
    case kDoubleClass: return double_class_;
    case kOneByteStringClass: return one_byte_string_class_;
    case kTwoByteStringClass: return two_byte_string_class_;
    case kFourByteStringClass: return four_byte_string_class_;
    case kExternalOneByteStringClass: return external_one_byte_string_class_;
    case kExternalTwoByteStringClass: return external_two_byte_string_class_;
    case kExternalFourByteStringClass: return external_four_byte_string_class_;
    case kBoolClass: return bool_class_;
    case kArrayClass: return array_class_;
    case kImmutableArrayClass: return immutable_array_class_;
    case kByteBufferClass: return byte_buffer_class_;
    case kStacktraceClass: return stacktrace_class_;
    case kJSRegExpClass: return jsregexp_class_;
    default: break;
  }
  UNREACHABLE();
  return Class::null();
}


int ObjectStore::GetClassIndex(const RawClass* raw_class) {
  ASSERT(raw_class->IsHeapObject());
  if (raw_class == object_class_) {
    return kObjectClass;
  } else if (raw_class == smi_class_) {
    return kSmiClass;
  } else if (raw_class == mint_class_) {
    return kMintClass;
  } else if (raw_class == bigint_class_) {
    return kBigintClass;
  } else if (raw_class == double_class_) {
    return kDoubleClass;
  } else if (raw_class == one_byte_string_class_) {
    return kOneByteStringClass;
  } else if (raw_class == two_byte_string_class_) {
    return kTwoByteStringClass;
  } else if (raw_class == four_byte_string_class_) {
    return kFourByteStringClass;
  } else if (raw_class == external_one_byte_string_class_) {
    return kExternalOneByteStringClass;
  } else if (raw_class == external_two_byte_string_class_) {
    return kExternalTwoByteStringClass;
  } else if (raw_class == external_four_byte_string_class_) {
    return kExternalFourByteStringClass;
  } else if (raw_class == bool_class_) {
    return kBoolClass;
  } else if (raw_class == array_class_) {
    return kArrayClass;
  } else if (raw_class == immutable_array_class_) {
    return kImmutableArrayClass;
  } else if (raw_class == byte_buffer_class_) {
    return kByteBufferClass;
  } else if (raw_class == stacktrace_class_) {
    return kStacktraceClass;
  } else if (raw_class == jsregexp_class_) {
    return kJSRegExpClass;
  }
  return kInvalidIndex;
}


RawType* ObjectStore::GetType(int index) {
  switch (index) {
    case kObjectType: return object_type();
    case kNullType: return null_type();
    case kDynamicType: return dynamic_type();
    case kVoidType: return void_type();
    case kFunctionInterface: return function_interface();
    case kNumberInterface: return number_interface();
    case kDoubleInterface: return double_interface();
    case kIntInterface: return int_interface();
    case kBoolInterface: return bool_interface();
    case kStringInterface: return string_interface();
    case kListInterface: return list_interface();
    default: break;
  }
  UNREACHABLE();
  return Type::null();
}


int ObjectStore::GetTypeIndex(const RawType* raw_type) {
  ASSERT(raw_type->IsHeapObject());
  if (raw_type == object_type()) {
    return kObjectType;
  } else if (raw_type == null_type()) {
    return kNullType;
  } else if (raw_type == dynamic_type()) {
    return kDynamicType;
  } else if (raw_type == void_type()) {
    return kVoidType;
  } else if (raw_type == function_interface()) {
    return kFunctionInterface;
  } else if (raw_type == number_interface()) {
    return kNumberInterface;
  } else if (raw_type == double_interface()) {
    return kDoubleInterface;
  } else if (raw_type == int_interface()) {
    return kIntInterface;
  } else if (raw_type == bool_interface()) {
    return kBoolInterface;
  } else if (raw_type == string_interface()) {
    return kStringInterface;
  } else if (raw_type == list_interface()) {
    return kListInterface;
  }
  return kInvalidIndex;
}

}  // namespace dart
