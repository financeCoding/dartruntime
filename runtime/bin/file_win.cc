// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bin/builtin.h"
#include "bin/file.h"

class FileHandle {
 public:
  explicit FileHandle(int fd) : fd_(fd) { }
  ~FileHandle() { }
  int fd() const { return fd_; }
  void set_fd(int fd) { fd_ = fd; }

 private:
  int fd_;

  // DISALLOW_COPY_AND_ASSIGN(FileHandle).
  FileHandle(const FileHandle&);
  void operator=(const FileHandle&);
};


File::~File() {
  // Close the file (unless it's a standard stream).
  if (handle_->fd() > 2) {
    Close();
  }
  delete handle_;
}


void File::Close() {
  ASSERT(handle_->fd() >= 0);
  int err = close(handle_->fd());
  if (err != 0) {
    fprintf(stderr, "%s\n", strerror(errno));
  }
  handle_->set_fd(kClosedFd);
}


bool File::IsClosed() {
  return handle_->fd() == kClosedFd;
}


int64_t File::Read(void* buffer, int64_t num_bytes) {
  ASSERT(handle_->fd() >= 0);
  return read(handle_->fd(), buffer, num_bytes);
}


int64_t File::Write(const void* buffer, int64_t num_bytes) {
  ASSERT(handle_->fd() >= 0);
  return write(handle_->fd(), buffer, num_bytes);
}


off_t File::Position() {
  ASSERT(handle_->fd() >= 0);
  return lseek(handle_->fd(), 0, SEEK_CUR);
}


bool File::SetPosition(int64_t position) {
  ASSERT(handle_->fd() >= 0);
  return (lseek(handle_->fd(), position, SEEK_SET) != -1);
}


bool File::Truncate(int64_t length) {
  ASSERT(handle_->fd() >= 0);
  return (chsize(handle_->fd(), length) != -1);
}


void File::Flush() {
  ASSERT(handle_->fd());
  _commit(handle_->fd());
}


off_t File::Length() {
  ASSERT(handle_->fd() >= 0);
  off_t position = lseek(handle_->fd(), 0, SEEK_CUR);
  if (position < 0) {
    // The file is not capable of seeking. Return an error.
    return -1;
  }
  off_t result = lseek(handle_->fd(), 0, SEEK_END);
  lseek(handle_->fd(), position, SEEK_SET);
  return result;
}


File* File::Open(const char* name, FileOpenMode mode) {
  int flags = O_RDONLY | O_BINARY;
  if ((mode & kWrite) != 0) {
    flags = (O_RDWR | O_CREAT | O_BINARY);
  }
  if ((mode & kTruncate) != 0) {
    flags = flags | O_TRUNC;
  }
  int fd = open(name, flags, 0666);
  if (fd < 0) {
    return NULL;
  }
  return new File(name, new FileHandle(fd));
}


bool File::Exists(const char* name) {
  struct stat st;
  if (stat(name, &st) == 0) {
    return ((st.st_mode & S_IFMT) == S_IFREG);
  } else {
    return false;
  }
}


bool File::Create(const char* name) {
  int fd = open(name, O_RDONLY | O_CREAT, 0666);
  if (fd < 0) {
    return false;
  }
  return (close(fd) == 0);
}


bool File::Delete(const char* name) {
  int status = remove(name);
  if (status == -1) {
    return false;
  }
  return true;
}


bool File::IsAbsolutePath(const char* pathname) {
  // Should we consider network paths?
  if (pathname == NULL) return false;
  return (strlen(pathname) > 2) &&
      (pathname[1] == ':') &&
      (pathname[2] == '\\');
}


char* File::GetCanonicalPath(const char* pathname) {
  int required_size = GetFullPathName(pathname, 0, NULL, NULL);
  char* path = static_cast<char*>(malloc(required_size));
  int written = GetFullPathName(pathname, required_size, path, NULL);
  ASSERT(written == (required_size - 1));
  return path;
}


const char* File::PathSeparator() {
  return "\\";
}


const char* File::StringEscapedPathSeparator() {
  return "\\\\";
}
