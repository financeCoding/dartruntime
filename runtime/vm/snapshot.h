// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_SNAPSHOT_H_
#define VM_SNAPSHOT_H_

#include "vm/allocation.h"
#include "vm/assert.h"
#include "vm/bitfield.h"
#include "vm/globals.h"
#include "vm/growable_array.h"
#include "vm/isolate.h"
#include "vm/visitor.h"

namespace dart {

// Forward declarations.
class Heap;
class Library;
class Object;
class ObjectStore;
class RawClass;
class RawObject;

static const int8_t kSerializedBitsPerByte = 7;
static const int8_t kMaxSerializedUnsignedValuePerByte = 127;
static const int8_t kMaxSerializedValuePerByte = 63;
static const int8_t kMinSerializedValuePerByte = -64;
static const uint8_t kEndByteMarker = (255 - kMaxSerializedValuePerByte);
static const int8_t kSerializedByteMask = (1 << kSerializedBitsPerByte) - 1;

// Serialized object header encoding is as follows:
// - Smi: the Smi value is written as is (last bit is not tagged).
// - VM internal type (from VM isolate): (index of type in vm isolate | 0x3)
// - Object that has already been written: (negative id in stream | 0x3)
// - Object that is seen for the first time (inlined in the stream):
//   (a unique id for this object | 0x1)
enum SerializedHeaderType {
  kInlined = 0x1,
  kObjectId = 0x3,
};
static const int8_t kHeaderTagBits = 2;
static const int8_t kObjectIdTagBits = (kBitsPerWord - kHeaderTagBits);
static const intptr_t kMaxObjectId = (kIntptrMax >> kHeaderTagBits);

typedef uint8_t* (*ReAlloc)(uint8_t* ptr, intptr_t old_size, intptr_t new_size);

class SerializedHeaderTag : public BitField<enum SerializedHeaderType,
                                            0,
                                            kHeaderTagBits> {
};


class SerializedHeaderData : public BitField<intptr_t,
                                             kHeaderTagBits,
                                             kObjectIdTagBits> {
};


// Structure capturing the raw snapshot.
class Snapshot {
 public:
  enum Kind {
    kFull = 0,  // Full snapshot of the current dart heap.
    kScript,    // A partial snapshot of only the application script.
    kMessage,   // A partial snapshot used only for isolate messaging.
  };

  static const int kHeaderSize = 2 * sizeof(int32_t);
  static const int kLengthIndex = 0;
  static const int kSnapshotFlagIndex = 1;

  static const Snapshot* SetupFromBuffer(const void* raw_memory);

  // Getters.
  const uint8_t* content() const { return content_; }
  int32_t length() const { return length_; }
  Kind kind() const { return static_cast<Kind>(kind_); }

  bool IsMessageSnapshot() const { return kind_ == kMessage; }
  bool IsScriptSnapshot() const { return kind_ == kScript; }
  bool IsFullSnapshot() const { return kind_ == kFull; }
  int32_t Size() const { return length_ + sizeof(Snapshot); }
  uint8_t* Addr() { return reinterpret_cast<uint8_t*>(this); }

  static intptr_t length_offset() { return OFFSET_OF(Snapshot, length_); }
  static intptr_t kind_offset() {
    return OFFSET_OF(Snapshot, kind_);
  }

 private:
  Snapshot() : length_(0), kind_(kFull) {}

  int32_t length_;  // Stream length.
  int32_t kind_;  // Kind of snapshot.
  uint8_t content_[];  // Stream content.

  DISALLOW_COPY_AND_ASSIGN(Snapshot);
};


// Stream for reading various types from a buffer.
class ReadStream : public ValueObject {
 public:
  ReadStream(const uint8_t* buffer, intptr_t size) : buffer_(buffer),
                                                     current_(buffer),
                                                     end_(buffer + size)  {}

 private:
  template<typename T>
  T Read() {
    uint8_t b = ReadByte();
    if (b > kMaxSerializedUnsignedValuePerByte) {
      return static_cast<T>(b) - kEndByteMarker;
    }
    T r = 0;
    uint8_t s = 0;
    do {
      r |= static_cast<T>(b) << s;
      s += kSerializedBitsPerByte;
      b = ReadByte();
    } while (b <= kMaxSerializedUnsignedValuePerByte);
    return r | ((static_cast<T>(b) - kEndByteMarker) << s);
  }

  template<int N, typename T>
  class Raw { };

  template<typename T>
  class Raw<1, T> {
   public:
    static T Read(ReadStream* st) {
      return bit_cast<T>(st->ReadByte());
    }
  };

  template<typename T>
  class Raw<2, T> {
   public:
    static T Read(ReadStream* st) {
      return bit_cast<T>(st->Read<int16_t>());
    }
  };

  template<typename T>
  class Raw<4, T> {
   public:
    static T Read(ReadStream* st) {
      return bit_cast<T>(st->Read<int32_t>());
    }
  };

  template<typename T>
  class Raw<8, T> {
   public:
    static T Read(ReadStream* st) {
      return bit_cast<T>(st->Read<int64_t>());
    }
  };

  uint8_t ReadByte() {
    ASSERT(current_ < end_);
    return *current_++;
  }

 private:
  const uint8_t* buffer_;
  const uint8_t* current_;
  const uint8_t* end_;

  // SnapshotReader needs access to the private Raw classes.
  friend class SnapshotReader;
  DISALLOW_COPY_AND_ASSIGN(ReadStream);
};


// Stream for writing various types into a buffer.
class WriteStream : public ValueObject {
 public:
  static const int kBufferIncrementSize = 64 * KB;

  WriteStream(uint8_t** buffer, ReAlloc alloc) :
      buffer_(buffer),
      end_(NULL),
      current_(NULL),
      current_size_(0),
      alloc_(alloc) {
    ASSERT(buffer != NULL);
    ASSERT(alloc != NULL);
    *buffer_ = reinterpret_cast<uint8_t*>(alloc_(NULL,
                                                 0,
                                                 kBufferIncrementSize));
    ASSERT(*buffer_ != NULL);
    current_ = *buffer_ + Snapshot::kHeaderSize;
    current_size_ = kBufferIncrementSize;
    end_ = *buffer_ + kBufferIncrementSize;
  }

  uint8_t* buffer() const { return *buffer_; }
  int bytes_written() const { return current_ - *buffer_; }

 private:
  template<typename T>
  void Write(T value) {
    T v = value;
    while (v < kMinSerializedValuePerByte ||
           v > kMaxSerializedValuePerByte) {
      WriteByte(static_cast<uint8_t>(v & kSerializedByteMask));
      v = v >> kSerializedBitsPerByte;
    }
    WriteByte(static_cast<uint8_t>(v + kEndByteMarker));
  }

  template<int N, typename T>
  class Raw { };

  template<typename T>
  class Raw<1, T> {
   public:
    static void Write(WriteStream* st, T value) {
      st->WriteByte(bit_cast<int8_t>(value));
    }
  };

  template<typename T>
  class Raw<2, T> {
   public:
    static void Write(WriteStream* st, T value) {
      st->Write<int16_t>(bit_cast<int16_t>(value));
    }
  };

  template<typename T>
  class Raw<4, T> {
   public:
    static void Write(WriteStream* st, T value) {
      st->Write<int32_t>(bit_cast<int32_t>(value));
    }
  };

  template<typename T>
  class Raw<8, T> {
   public:
    static void Write(WriteStream* st, T value) {
      st->Write<int64_t>(bit_cast<int64_t>(value));
    }
  };

  void WriteByte(uint8_t value) {
    if (current_ >= end_) {
      intptr_t new_size = (current_size_ + kBufferIncrementSize);
      *buffer_ = reinterpret_cast<uint8_t*>(alloc_(*buffer_,
                                                   current_size_,
                                                   new_size));
      ASSERT(*buffer_ != NULL);
      current_ = *buffer_ + current_size_;
      current_size_ = new_size;
      end_ = *buffer_ + new_size;
    }
    ASSERT(current_ < end_);
    *current_++ = value;
  }

 private:
  uint8_t** const buffer_;
  uint8_t* end_;
  uint8_t* current_;
  intptr_t current_size_;
  ReAlloc alloc_;

  // MessageWriter and SnapshotWriter needs access to the private Raw
  // classes.
  friend class BaseWriter;
  DISALLOW_COPY_AND_ASSIGN(WriteStream);
};


// Reads a snapshot into objects.
class SnapshotReader {
 public:
  SnapshotReader(const Snapshot* snapshot,
                 Heap* heap,
                 ObjectStore* object_store)
      : stream_(snapshot->content(), snapshot->length()),
        kind_(snapshot->kind()),
        heap_(heap),
        object_store_(object_store),
        backward_references_() { }
  ~SnapshotReader() { }

  // Reads raw data (for basic types).
  // sizeof(T) must be in {1,2,4,8}.
  template <typename T>
  T Read() {
    return ReadStream::Raw<sizeof(T), T>::Read(&stream_);
  }

  // Reads an intptr_t type value.
  intptr_t ReadIntptrValue() {
    int64_t value = Read<int64_t>();
    ASSERT((value <= kIntptrMax) && (value >= kIntptrMin));
    return value;
  }

  Heap* heap() const { return heap_; }
  ObjectStore* object_store() const { return object_store_; }

  // Reads an object.
  RawObject* ReadObject();

  RawClass* ReadClassId(intptr_t object_id);

  // Add object to backward references.
  void AddBackwardReference(intptr_t id, Object* obj);

  // Read a full snap shot.
  void ReadFullSnapshot();

 private:
  // Internal implementation of ReadObject once the header value is read.
  RawObject* ReadObjectImpl(intptr_t header);

  // Read an object that was serialized as an Id (singleton, object store,
  // or an object that was already serialized before).
  RawObject* ReadIndexedObject(intptr_t object_id);

  // Read an inlined object from the stream.
  RawObject* ReadInlinedObject(intptr_t object_id);

  // Based on header field check to see if it is an internal VM class.
  RawClass* LookupInternalClass(intptr_t class_header);

  ReadStream stream_;  // input stream.
  Snapshot::Kind kind_;  // Indicates type of snapshot(full, script, message).
  Heap* heap_;  // Heap into which the objects are deserialized into.
  ObjectStore* object_store_;  // Object store for common classes.
  GrowableArray<Object*> backward_references_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotReader);
};


class BaseWriter {
 public:
  // Size of the snapshot.
  intptr_t BytesWritten() const { return stream_.bytes_written(); }

  // Writes raw data to the stream (basic type).
  // sizeof(T) must be in {1,2,4,8}.
  template <typename T>
  void Write(T value) {
    WriteStream::Raw<sizeof(T), T>::Write(&stream_, value);
  }

  // Writes an intptr_t type value out.
  void WriteIntptrValue(intptr_t value) {
    Write<int64_t>(value);
  }

  // Write an object that is serialized as an Id (singleton, object store,
  // or an object that was already serialized before).
  void WriteIndexedObject(intptr_t object_id) {
    WriteSerializationMarker(kObjectId, object_id);
  }

  // Write out object header value.
  void WriteObjectHeader(intptr_t class_id, intptr_t tags) {
    // Write out the class information.
    WriteIndexedObject(class_id);
    // Write out the tags information.
    WriteIntptrValue(tags);
  }

  // Write serialization header information for an object.
  void WriteSerializationMarker(SerializedHeaderType type, intptr_t id) {
    ASSERT(id <= kMaxObjectId);
    intptr_t value = 0;
    value = SerializedHeaderTag::update(type, value);
    value = SerializedHeaderData::update(id, value);
    WriteIntptrValue(value);
  }

  // Finalize the serialized buffer by filling in the header information
  // which comprises of a flag(snaphot kind) and the length of
  // serialzed bytes.
  void FinalizeBuffer(Snapshot::Kind kind) {
    int32_t* data = reinterpret_cast<int32_t*>(stream_.buffer());
    data[Snapshot::kLengthIndex] = stream_.bytes_written();
    data[Snapshot::kSnapshotFlagIndex] = kind;
  }

 protected:
  BaseWriter(uint8_t** buffer, ReAlloc alloc) : stream_(buffer, alloc) {
    ASSERT(buffer != NULL);
    ASSERT(alloc != NULL);
  }
  ~BaseWriter() { }

 private:
  WriteStream stream_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(BaseWriter);
};


class MessageWriter : public BaseWriter {
 public:
  MessageWriter(uint8_t** buffer, ReAlloc alloc) : BaseWriter(buffer, alloc) {
  }
  ~MessageWriter() { }

  // Writes a message of integers.
  void WriteMessage(intptr_t field_count, intptr_t *data);

  void FinalizeBuffer() {
    BaseWriter::FinalizeBuffer(Snapshot::kMessage);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MessageWriter);
};


class SnapshotWriter : public BaseWriter {
 public:
  SnapshotWriter(Snapshot::Kind kind, uint8_t** buffer, ReAlloc alloc)
      : BaseWriter(buffer, alloc),
        kind_(kind),
        object_store_(Isolate::Current()->object_store()),
        forward_list_() {
  }
  ~SnapshotWriter() { }

  // Snapshot kind.
  Snapshot::Kind kind() const { return kind_; }

  // Finalize the serialized buffer by filling in the header information
  // which comprises of a flag(full/partial snaphot) and the length of
  // serialzed bytes.
  void FinalizeBuffer() {
    BaseWriter::FinalizeBuffer(kind_);
    UnmarkAll();
  }

  // Serialize an object into the buffer.
  void WriteObject(RawObject* raw);

  void WriteClassId(RawClass* cls);

  // Unmark all objects that were marked as forwarded for serializing.
  void UnmarkAll();

  // Writes a full snapshot of the Isolate.
  void WriteFullSnapshot();

 private:
  class ForwardObjectNode : public ZoneAllocated {
   public:
    ForwardObjectNode(RawObject* raw, RawClass* cls) : raw_(raw), cls_(cls) {}
    RawObject* raw() const { return raw_; }
    RawClass* cls() const { return cls_; }

   private:
    RawObject* raw_;
    RawClass* cls_;

    DISALLOW_COPY_AND_ASSIGN(ForwardObjectNode);
  };

  intptr_t MarkObject(RawObject* raw, RawClass* cls);

  void WriteInlinedObject(RawObject* raw);

  ObjectStore* object_store() const { return object_store_; }

  Snapshot::Kind kind_;
  ObjectStore* object_store_;  // Object store for common classes.
  GrowableArray<ForwardObjectNode*> forward_list_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotWriter);
};


class ScriptSnapshotWriter : public SnapshotWriter {
 public:
  ScriptSnapshotWriter(uint8_t** buffer, ReAlloc alloc)
      : SnapshotWriter(Snapshot::kScript, buffer, alloc) {
    ASSERT(buffer != NULL);
    ASSERT(alloc != NULL);
  }
  ~ScriptSnapshotWriter() { }

  // Writes a partial snapshot of the script.
  void WriteScriptSnapshot(const Library& lib);

 private:
  DISALLOW_COPY_AND_ASSIGN(ScriptSnapshotWriter);
};


// An object pointer visitor implementation which writes out
// objects to a snap shot.
class SnapshotWriterVisitor : public ObjectPointerVisitor {
 public:
  explicit SnapshotWriterVisitor(SnapshotWriter* writer) : writer_(writer) {}

  virtual void VisitPointers(RawObject** first, RawObject** last);

 private:
  SnapshotWriter* writer_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotWriterVisitor);
};

}  // namespace dart

#endif  // VM_SNAPSHOT_H_
