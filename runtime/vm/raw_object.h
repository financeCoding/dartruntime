// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_RAW_OBJECT_H_
#define VM_RAW_OBJECT_H_

#include "vm/assert.h"
#include "vm/globals.h"
#include "vm/snapshot.h"

#include "include/dart_api.h"

namespace dart {

// Macrobatics to define the Object hierarchy of VM implementation classes.
#define CLASS_LIST_NO_OBJECT(V)                                                \
  V(Class)                                                                     \
  V(UnresolvedClass)                                                           \
  V(AbstractType)                                                              \
    V(Type)                                                                    \
    V(TypeParameter)                                                           \
    V(InstantiatedType)                                                        \
  V(AbstractTypeArguments)                                                     \
    V(TypeArguments)                                                           \
    V(InstantiatedTypeArguments)                                               \
  V(Function)                                                                  \
  V(Field)                                                                     \
  V(TokenStream)                                                               \
  V(Script)                                                                    \
  V(Library)                                                                   \
  V(LibraryPrefix)                                                             \
  V(Code)                                                                      \
  V(Instructions)                                                              \
  V(PcDescriptors)                                                             \
  V(LocalVarDescriptors)                                                       \
  V(ExceptionHandlers)                                                         \
  V(Context)                                                                   \
  V(ContextScope)                                                              \
  V(Error)                                                                     \
    V(ApiError)                                                                \
    V(LanguageError)                                                           \
    V(UnhandledException)                                                      \
    V(UnwindError)                                                             \
  V(Instance)                                                                  \
    V(Number)                                                                  \
      V(Integer)                                                               \
        V(Smi)                                                                 \
        V(Mint)                                                                \
        V(Bigint)                                                              \
      V(Double)                                                                \
    V(String)                                                                  \
      V(OneByteString)                                                         \
      V(TwoByteString)                                                         \
      V(FourByteString)                                                        \
      V(ExternalOneByteString)                                                 \
      V(ExternalTwoByteString)                                                 \
      V(ExternalFourByteString)                                                \
    V(Bool)                                                                    \
    V(Array)                                                                   \
      V(ImmutableArray)                                                        \
    V(ByteBuffer)                                                              \
    V(Closure)                                                                 \
    V(Stacktrace)                                                              \
    V(JSRegExp)                                                                \

#define CLASS_LIST(V)                                                          \
  V(Object)                                                                    \
  CLASS_LIST_NO_OBJECT(V)


// Forward declarations.
class Isolate;
#define DEFINE_FORWARD_DECLARATION(clazz)                                      \
  class Raw##clazz;
CLASS_LIST(DEFINE_FORWARD_DECLARATION)
#undef DEFINE_FORWARD_DECLARATION


enum ObjectKind {
#define DEFINE_OBJECT_KIND(clazz)                                              \
  k##clazz,
CLASS_LIST(DEFINE_OBJECT_KIND)
#undef DEFINE_OBJECT_KIND
  // The following entry does not describe a real object, but instead it
  // identifies free list elements in the heap.
  kFreeListElement,
  kNumOfObjectKinds = kFreeListElement
};

enum ObjectAlignment {
  // Alignment offsets are used to determine object age.
  kNewObjectAlignmentOffset = kWordSize,
  kOldObjectAlignmentOffset = 0,
  // Object sizes are aligned to kObjectAlignment.
  kObjectAlignment = 2 * kWordSize,
  kObjectAlignmentLog2 = kWordSizeLog2 + 1,
  kObjectAlignmentMask = kObjectAlignment - 1,
};

enum {
  kSmiTag = 0,
  kHeapObjectTag = 1,
  kSmiTagSize = 1,
  kSmiTagMask = 1,
  kSmiTagShift = 1,
};

#define SNAPSHOT_WRITER_SUPPORT()                                              \
  void WriteTo(                                                                \
      SnapshotWriter* writer, intptr_t object_id, Snapshot::Kind kind);        \
  friend class SnapshotWriter;                                                 \

#define VISITOR_SUPPORT(object)                                                \
  static intptr_t Visit##object##Pointers(Raw##object* raw_obj,                \
                                          ObjectPointerVisitor* visitor);

#define RAW_OBJECT_IMPLEMENTATION(object)                                      \
 private:  /* NOLINT */                                                        \
  VISITOR_SUPPORT(object)                                                      \
  friend class object;                                                         \
  friend class RawObject;                                                      \
  DISALLOW_ALLOCATION();                                                       \
  DISALLOW_IMPLICIT_CONSTRUCTORS(Raw##object)

#define RAW_HEAP_OBJECT_IMPLEMENTATION(object)                                 \
  private:                                                                     \
    RAW_OBJECT_IMPLEMENTATION(object);                                         \
    Raw##object* ptr() const {                                                 \
      ASSERT(IsHeapObject());                                                  \
      return reinterpret_cast<Raw##object*>(                                   \
          reinterpret_cast<uword>(this) - kHeapObjectTag);                     \
    }                                                                          \
    SNAPSHOT_WRITER_SUPPORT()                                                  \


// RawObject is the base class of all raw objects, even though it carries the
// class_ field not all raw objects are allocated in the heap and thus cannot
// be dereferenced (e.g. RawSmi).
class RawObject {
 public:
  // The tags field which is a part of the object header uses the following
  // bit fields for storing tags.
  enum TagBits {
    kFreeBit = 0,
    kMarkBit = 1,
    kCanonicalBit = 2,
    kFromSnapshotBit = 3,
    kReservedBit10K = 4,
    kReservedBit100K = 5,
    kReservedBit1M = 6,
    kReservedBit10M = 7,
    kSizeTagBit = 8,
    kSizeTagSize = 8,
  };

  // Encodes the object size in the tag in units of object alignment.
  class SizeTag {
   public:
    static const intptr_t kMaxSizeTag =
        ((1 << RawObject::kSizeTagSize) - 1) << kObjectAlignmentLog2;

    static uword encode(intptr_t size) {
      return SizeBits::encode(SizeToTagValue(size));
    }

    static intptr_t decode(uword tag) {
      return TagValueToSize(SizeBits::decode(tag));
    }

    static uword update(intptr_t size, uword tag) {
      return SizeBits::update(SizeToTagValue(size), tag);
    }

  private:
    // The actual unscaled bit field used within the tag field.
    class SizeBits : public BitField<intptr_t, kSizeTagBit, kSizeTagSize> {};

    static intptr_t SizeToTagValue(intptr_t size) {
      ASSERT(Utils::IsAligned(size, kObjectAlignment));
      return  (size > kMaxSizeTag) ? 0 : (size >> kObjectAlignmentLog2);
    }
    static intptr_t TagValueToSize(intptr_t value) {
      return value << kObjectAlignmentLog2;
    }
  };

  bool IsHeapObject() const {
    uword value = reinterpret_cast<uword>(this);
    return (value & kSmiTagMask) == kHeapObjectTag;
  }

  bool IsNewObject() const {
    uword addr = reinterpret_cast<uword>(this);
    return (addr & kNewObjectAlignmentOffset) == kNewObjectAlignmentOffset;
  }
  bool IsOldObject() const {
    uword addr = reinterpret_cast<uword>(this);
    return (addr & kNewObjectAlignmentOffset) == kOldObjectAlignmentOffset;
  }

  // Support for GC marking bit.
  bool IsMarked() const {
    return MarkBit::decode(ptr()->tags_);
  }
  void SetMarkBit() {
    ASSERT(!IsMarked());
    uword tags = ptr()->tags_;
    ptr()->tags_ = MarkBit::update(true, tags);
  }
  void ClearMarkBit() {
    ASSERT(IsMarked());
    uword tags = ptr()->tags_;
    ptr()->tags_ = MarkBit::update(false, tags);
  }

  // Support for object tags.
  bool IsCanonical() const {
    return CanonicalObjectTag::decode(ptr()->tags_);
  }
  void SetCanonical() {
    uword tags = ptr()->tags_;
    ptr()->tags_ = CanonicalObjectTag::update(true, tags);
  }
  bool IsCreatedFromSnapshot() const {
    return CreatedFromSnapshotTag::decode(ptr()->tags_);
  }
  void SetCreatedFromSnapshot() {
    uword tags = ptr()->tags_;
    ptr()->tags_ = CreatedFromSnapshotTag::update(true, tags);
  }

  intptr_t Size() const {
    uword tags = ptr()->tags_;
    intptr_t result = SizeTag::decode(tags);
    if ((result != 0) && !FreeBit::decode(tags)) {
      ASSERT(result == SizeFromClass());
      return result;
    }
    result = SizeFromClass();
    ASSERT((result > SizeTag::kMaxSizeTag) || FreeBit::decode(tags));
    return result;
  }

  void Validate() const;
  intptr_t VisitPointers(ObjectPointerVisitor* visitor);

  static RawObject* FromAddr(uword addr) {
    // We expect the untagged address here.
    ASSERT((addr & kSmiTagMask) != kHeapObjectTag);
    return reinterpret_cast<RawObject*>(addr + kHeapObjectTag);
  }

  static uword ToAddr(RawObject* raw_obj) {
    return reinterpret_cast<uword>(raw_obj->ptr());
  }

  static bool IsCreatedFromSnapshot(intptr_t value) {
    return CreatedFromSnapshotTag::decode(value);
  }

  static bool IsCanonical(intptr_t value) {
    return CanonicalObjectTag::decode(value);
  }

 protected:
  RawClass* class_;
  uword tags_;  // Various object tags (bits).

 private:
  class FreeBit : public BitField<bool, kFreeBit, 1> {};

  class MarkBit : public BitField<bool, kMarkBit, 1> {};

  class CanonicalObjectTag : public BitField<bool, kCanonicalBit, 1> {};

  class CreatedFromSnapshotTag : public BitField<bool, kFromSnapshotBit, 1> {};

  RawObject* ptr() const {
    ASSERT(IsHeapObject());
    return reinterpret_cast<RawObject*>(
        reinterpret_cast<uword>(this) - kHeapObjectTag);
  }

  intptr_t SizeFromClass() const;

  friend class Object;
  friend class Array;
  friend class SnapshotWriter;
  friend class SnapshotReader;
  friend class MarkingVisitor;

  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(RawObject);
};


class RawClass : public RawObject {
 public:
  enum ClassState {
    kAllocated,     // Initial state.
    kPreFinalized,  // VM classes: size precomputed, but no checks done.
    kFinalized,     // All checks completed, class ready for use.
  };

 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(Class);

  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->name_); }
  RawString* name_;
  RawArray* functions_;
  RawArray* fields_;
  // TODO(srdjan): RawTypeArguments* interfaces_;  // Array of Type.
  RawArray* interfaces_;  // Array of AbstractType.
  RawScript* script_;
  RawLibrary* library_;
  RawArray* type_parameters_;  // Array of String.
  RawTypeArguments* type_parameter_extends_;  // DynamicType if no extends.
  RawType* super_type_;
  RawObject* factory_class_;  // UnresolvedClass (until finalization) or Class.
  RawFunction* signature_function_;  // Associated function for signature class.
  RawArray* functions_cache_;  // See class FunctionsCache.
  RawArray* constants_;  // Canonicalized values of this class.
  // TODO(srdjan): RawTypeArguments* canonical_types_;
  RawArray* canonical_types_;  // Canonicalized types of this class.
  RawCode* allocation_stub_;  // Stub code for allocation of instances.
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->allocation_stub_);
  }

  cpp_vtable handle_vtable_;
  intptr_t instance_size_;
  ObjectKind instance_kind_;
  intptr_t type_arguments_instance_field_offset_;  // May be kNoTypeArguments.
  intptr_t next_field_offset_;  // Offset of then next instance field.
  intptr_t num_native_fields_;  // Number of native fields in class.
  int8_t class_state_;  // Of type ClassState.
  bool is_const_;
  bool is_interface_;

  friend class Object;
  friend class RawInstance;
  friend RawClass* AllocateFakeClass();
};


class RawUnresolvedClass : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(UnresolvedClass);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->qualifier_);
  }
  RawString* qualifier_;  // Qualifier for the identifier.
  RawString* ident_;  // Name of the unresolved identifier.
  RawClass* factory_signature_class_;  // Expected type parameters for factory.
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->factory_signature_class_);
  }
  intptr_t token_index_;
};


class RawAbstractType : public RawObject {
 protected:
  enum TypeState {
    kAllocated,  // Initial state.
    kBeingFinalized,  // In the process of being finalized.
    kFinalized,  // Type ready for use.
  };

 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(AbstractType);

  friend class ObjectStore;
};


class RawType : public RawAbstractType {
 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(Type);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->type_class_);
  }
  RawObject* type_class_;  // Either resolved class or unresolved class.
  RawAbstractTypeArguments* arguments_;
  RawObject** to() { return reinterpret_cast<RawObject**>(&ptr()->arguments_); }
  int8_t type_state_;
};


class RawTypeParameter : public RawAbstractType {
 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(TypeParameter);

  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->name_); }
  RawString* name_;
  RawObject** to() { return reinterpret_cast<RawObject**>(&ptr()->name_); }
  intptr_t index_;
  int8_t type_state_;
};


class RawInstantiatedType : public RawAbstractType {
 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(InstantiatedType);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->uninstantiated_type_);
  }
  RawAbstractType* uninstantiated_type_;
  RawAbstractTypeArguments* instantiator_type_arguments_;
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->instantiator_type_arguments_);
  }
};


class RawAbstractTypeArguments : public RawObject {
 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(AbstractTypeArguments);
};


class RawTypeArguments : public RawAbstractTypeArguments {
 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(TypeArguments);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->length_);
  }
  RawSmi* length_;

  // Variable length data follows here.
  RawAbstractType* types_[0];
  RawObject** to(intptr_t length) {
    return reinterpret_cast<RawObject**>(&ptr()->types_[length - 1]);
  }
};


class RawInstantiatedTypeArguments : public RawAbstractTypeArguments {
 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(InstantiatedTypeArguments);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(
        &ptr()->uninstantiated_type_arguments_);
  }
  RawAbstractTypeArguments* uninstantiated_type_arguments_;
  RawAbstractTypeArguments* instantiator_type_arguments_;
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->instantiator_type_arguments_);
  }
};


class RawFunction : public RawObject {
 public:
  enum Kind {
    kFunction,
    kClosureFunction,
    kSignatureFunction,  // represents a signature only without actual code.
    kGetterFunction,  // represents getter functions e.g: get foo() { .. }.
    kSetterFunction,  // represents setter functions e.g: set foo(..) { .. }.
    kAbstract,
    kConstructor,
    kImplicitGetter,  // represents an implicit getter for fields.
    kImplicitSetter,  // represents an implicit setter for fields.
    kConstImplicitGetter,  // represents an implicit const getter for fields.
  };

 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(Function);

  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->name_); }
  RawString* name_;
  RawClass* owner_;
  RawAbstractType* result_type_;
  RawArray* parameter_types_;
  RawArray* parameter_names_;
  RawCode* code_;  // Compiled code for the function.
  RawCode* unoptimized_code_;  // Unoptimized code, keep it after optimization.
  RawContextScope* context_scope_;
  RawFunction* parent_function_;  // Enclosing function of this local function.
  RawClass* signature_class_;  // Only for closure or signature function.
  RawCode* closure_allocation_stub_;  // Stub code for allocation of closures.
  RawFunction* implicit_closure_function_;  // Implicit closure function.
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->implicit_closure_function_);
  }

  intptr_t token_index_;
  intptr_t num_fixed_parameters_;
  intptr_t num_optional_parameters_;
  intptr_t invocation_counter_;
  intptr_t deoptimization_counter_;
  Kind kind_;
  bool is_static_;
  bool is_const_;
  bool is_optimizable_;
};


class RawField : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Field);

  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->name_); }
  RawString* name_;
  RawClass* owner_;
  RawAbstractType* type_;
  RawInstance* value_;  // Offset for instance and value for static fields.
  RawObject** to() { return reinterpret_cast<RawObject**>(&ptr()->value_); }

  intptr_t token_index_;
  bool is_static_;
  bool is_final_;
  bool has_initializer_;
};


class RawTokenStream : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(TokenStream);

  enum {
    kKindEntry = 0,
    kLiteralEntry,
    kNumberOfEntries
  };

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->private_key_);
  }
  RawString* private_key_;  // Key used for private identifiers.
  RawSmi* length_;  // Number of tokens.

  // Variable length data follows here.
  RawObject* data_[0];
  RawObject** to(intptr_t length) {
    return reinterpret_cast<RawObject**>(
        &ptr()->data_[length * kNumberOfEntries - 1]);
  }
};


class RawScript : public RawObject {
 public:
  enum Kind {
    kScript = 0,
    kLibrary,
    kSource
  };

 private:
  RAW_HEAP_OBJECT_IMPLEMENTATION(Script);

  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->url_); }
  RawString* url_;
  RawString* source_;
  RawTokenStream* tokens_;
  RawObject** to() { return reinterpret_cast<RawObject**>(&ptr()->tokens_); }

  Kind kind_;
};


class RawLibrary : public RawObject {
  enum LibraryState {
    kAllocated,       // Initial state.
    kLoadInProgress,  // Library is in the process of being loaded.
    kLoaded,          // Library is loaded.
    kLoadError,       // Error occurred during load of the Library.
  };

  RAW_HEAP_OBJECT_IMPLEMENTATION(Library);

  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->name_); }
  RawString* name_;
  RawString* url_;
  RawScript* script_;
  RawString* private_key_;
  RawArray* dictionary_;         // Top-level names in this library.
  RawArray* anonymous_classes_;  // Classes containing top-level elements.
  RawArray* imports_;            // List of libraries imported without prefix.
  RawArray* imported_into_;      // List of libraries where this library
                                 // is imported into without a prefix.
  RawLibrary* next_registered_;  // Linked list of registered libraries.
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->next_registered_);
  }

  intptr_t num_imports_;         // Number of entries in imports_.
  intptr_t num_imported_into_;   // Number of entries in imported_into_.
  intptr_t num_anonymous_;       // Number of entries in anonymous_classes_.
  Dart_NativeEntryResolver native_entry_resolver_;  // Resolves natives.
  bool corelib_imported_;
  int8_t load_state_;            // Of type LibraryState.
};


class RawLibraryPrefix : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(LibraryPrefix);

  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->name_); }
  RawString* name_;               // library prefix name.
  RawLibrary* library_;           // library imported with a prefix.
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->library_);
  }
};


class RawCode : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Code);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->instructions_);
  }
  RawInstructions* instructions_;
  RawFunction* function_;
  RawExceptionHandlers* exception_handlers_;
  RawPcDescriptors* pc_descriptors_;
  RawLocalVarDescriptors* var_descriptors_;
  // Ongoing redesign of inline caches may soon remove the need for 'ic_data_'.
  RawArray* ic_data_;  // Used to store IC stub data (see class ICData).
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->ic_data_);
  }

  intptr_t pointer_offsets_length_;
  // This cannot be boolean because of alignment issues on x64 architectures.
  intptr_t is_optimized_;

  // Variable length data follows here.
  int32_t data_[0];
};


class RawInstructions : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Instructions);

  RawCode* code_;
  intptr_t size_;

  // Variable length data follows here.
  uint8_t data_[0];

  friend class RawCode;
};


class RawPcDescriptors : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(PcDescriptors);

  RawSmi* length_;  // Number of descriptors.

  // Variable length data follows here.
  intptr_t data_[0];
};


class RawLocalVarDescriptors : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(LocalVarDescriptors);

  struct VarInfo {
    intptr_t index;      // Slot index on stack or in context.
    intptr_t scope_id;   // Scope to which the variable belongs.
    intptr_t begin_pos;  // Token position of scope start.
    intptr_t end_pos;    // Token position of scope end.
  };

  intptr_t length_;  // Number of descriptors.
  RawArray* names_;  // Array of [length_] variable names.

  VarInfo data_[0];   // Variable info with [length_] entries.
};


class RawExceptionHandlers : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(ExceptionHandlers);

  RawSmi* length_;  // Number of exception handler entries.

  // Variable length data follows here.
  intptr_t data_[0];
};


class RawContext : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Context);

  intptr_t num_variables_;
  Isolate* isolate_;

  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->parent_); }
  RawContext* parent_;

  // Variable length data follows here.
  RawInstance* data_[0];
  RawObject** to(intptr_t num_vars) {
    return reinterpret_cast<RawObject**>(&ptr()->data_[num_vars - 1]);
  }
};


class RawContextScope : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(ContextScope);

  // TODO(iposva): Switch to convential enum offset based structure to avoid
  // alignment mishaps.
  struct VariableDesc {
    RawSmi* token_index;
    RawString* name;
    RawBool* is_final;
    RawAbstractType* type;
    RawSmi* context_index;
    RawSmi* context_level;
  };

  intptr_t num_variables_;

  // Variable length data follows here.
  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->data_[0]); }
  RawObject* data_[0];
  RawObject** to(intptr_t num_vars) {
    intptr_t data_length = num_vars * (sizeof(VariableDesc)/kWordSize);
    return reinterpret_cast<RawObject**>(&ptr()->data_[data_length - 1]);
  }
};


class RawError : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Error);
};


class RawApiError : public RawError {
  RAW_HEAP_OBJECT_IMPLEMENTATION(ApiError);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->message_);
  }
  RawString* message_;
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->message_);
  }
};


class RawLanguageError : public RawError {
  RAW_HEAP_OBJECT_IMPLEMENTATION(LanguageError);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->message_);
  }
  RawString* message_;
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->message_);
  }
};


class RawUnhandledException : public RawError {
  RAW_HEAP_OBJECT_IMPLEMENTATION(UnhandledException);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->exception_);
  }
  RawInstance* exception_;
  RawInstance* stacktrace_;
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->stacktrace_);
  }
};


class RawUnwindError : public RawError {
  RAW_HEAP_OBJECT_IMPLEMENTATION(UnwindError);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->message_);
  }
  RawString* message_;
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->message_);
  }
};


class RawInstance : public RawObject {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Instance);
};


class RawNumber : public RawInstance {
  RAW_OBJECT_IMPLEMENTATION(Number);
};


class RawInteger : public RawNumber {
  RAW_OBJECT_IMPLEMENTATION(Integer);
};


class RawSmi : public RawInteger {
  RAW_OBJECT_IMPLEMENTATION(Smi);
};


class RawMint : public RawInteger {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Mint);

  int64_t value_;
};


class RawBigint : public RawInteger {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Bigint);

  // Note that the structure is not necessarily valid unless tweaked
  // by Bigint::BNAddr().
  BIGNUM bn_;

  // Variable length data follows here.
  BN_ULONG data_[0];
};


class RawDouble : public RawNumber {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Double);

  double value_;
};


class RawString : public RawInstance {
  RAW_HEAP_OBJECT_IMPLEMENTATION(String);

 protected:
  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->length_); }
  RawSmi* length_;
  RawSmi* hash_;
  RawObject** to() { return reinterpret_cast<RawObject**>(&ptr()->hash_); }
};


class RawOneByteString : public RawString {
  RAW_HEAP_OBJECT_IMPLEMENTATION(OneByteString);

  // Variable length data follows here.
  uint8_t data_[0];
};


class RawTwoByteString : public RawString {
  RAW_HEAP_OBJECT_IMPLEMENTATION(TwoByteString);

  // Variable length data follows here.
  uint16_t data_[0];
};


class RawFourByteString : public RawString {
  RAW_HEAP_OBJECT_IMPLEMENTATION(FourByteString);

  // Variable length data follows here.
  uint32_t data_[0];
};


template<typename T>
class ExternalStringData {
 public:
  typedef void Callback(void* peer);
  ExternalStringData(const T* data, void* peer, Callback* callback) :
      data_(data), peer_(peer), callback_(callback) {
  }
  const T* data_;
  void* peer_;
  Callback* callback_;
};


class RawExternalOneByteString : public RawString {
  RAW_HEAP_OBJECT_IMPLEMENTATION(ExternalOneByteString);

  ExternalStringData<uint8_t>* external_data_;
};


class RawExternalTwoByteString : public RawString {
  RAW_HEAP_OBJECT_IMPLEMENTATION(ExternalTwoByteString);

  ExternalStringData<uint16_t>* external_data_;
};


class RawExternalFourByteString : public RawString {
  RAW_HEAP_OBJECT_IMPLEMENTATION(ExternalFourByteString);

  ExternalStringData<uint32_t>* external_data_;
};


class RawBool : public RawInstance {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Bool);

  bool value_;
};


class RawArray : public RawInstance {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Array);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->type_arguments_);
  }
  RawAbstractTypeArguments* type_arguments_;
  RawSmi* length_;
  // Variable length data follows here.
  RawObject** data() {
    uword address_of_length = reinterpret_cast<uword>(&length_);
    return reinterpret_cast<RawObject**>(address_of_length + kWordSize);
  }
  RawObject** to(intptr_t length) {
    return reinterpret_cast<RawObject**>(&ptr()->data()[length - 1]);
  }

  friend class RawImmutableArray;
};


class RawImmutableArray : public RawArray {
  RAW_HEAP_OBJECT_IMPLEMENTATION(ImmutableArray);
};


class RawByteBuffer : public RawInstance {
  RAW_HEAP_OBJECT_IMPLEMENTATION(ByteBuffer);

  RawObject** from() { return reinterpret_cast<RawObject**>(&ptr()->length_); }
  RawSmi* length_;
  RawObject** to() { return reinterpret_cast<RawObject**>(&ptr()->length_); }
  uint8_t* data_;
};


class RawClosure : public RawInstance {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Closure);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->type_arguments_);
  }
  RawAbstractTypeArguments* type_arguments_;
  RawFunction* function_;
  RawContext* context_;
  // TODO(iposva): Remove this temporary hack.
  RawInteger* smrck_;
  RawObject** to() { return reinterpret_cast<RawObject**>(&ptr()->smrck_); }
};


// VM type for capturing stacktraces when exceptions are thrown,
// Currently we don't have any interface that this object is supposed
// to implement so we just support the 'toString' method which
// converts the stack trace into a string.
class RawStacktrace : public RawInstance {
  RAW_HEAP_OBJECT_IMPLEMENTATION(Stacktrace);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->function_array_);
  }
  RawArray* function_array_;  // Function for each frame in the stack trace.
  RawArray* code_array_;  // Code object for each frame in the stack trace.
  RawArray* pc_offset_array_;  // Offset of PC for each frame.
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->pc_offset_array_);
  }
};


// VM type for capturing JS regular expressions.
class RawJSRegExp : public RawInstance {
  RAW_HEAP_OBJECT_IMPLEMENTATION(JSRegExp);

  RawObject** from() {
    return reinterpret_cast<RawObject**>(&ptr()->data_length_);
  }
  RawSmi* data_length_;
  RawSmi* num_bracket_expressions_;
  RawString* pattern_;  // Pattern to be used for matching.
  RawObject** to() {
    return reinterpret_cast<RawObject**>(&ptr()->pattern_);
  }

  intptr_t type_;  // Uninitialized, simple or complex.
  intptr_t flags_;  // Represents global/local, case insensitive, multiline.

  // Variable length data follows here.
  uint8_t data_[0];
};

}  // namespace dart

#endif  // VM_RAW_OBJECT_H_
