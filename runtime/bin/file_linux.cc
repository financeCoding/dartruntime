// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

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
  if (handle_->fd() > STDERR_FILENO) {
    Close();
  }
  delete handle_;
}


void File::Close() {
  ASSERT(handle_->fd() >= 0);
  int err = TEMP_FAILURE_RETRY(close(handle_->fd()));
  if (err != 0) {
    const int kBufferSize = 1024;
    char error_message[kBufferSize];
    strerror_r(errno, error_message, kBufferSize);
    fprintf(stderr, "%s\n", error_message);
  }
  handle_->set_fd(kClosedFd);
}


bool File::IsClosed() {
  return handle_->fd() == kClosedFd;
}


int64_t File::Read(void* buffer, int64_t num_bytes) {
  ASSERT(handle_->fd() >= 0);
  return TEMP_FAILURE_RETRY(read(handle_->fd(), buffer, num_bytes));
}


int64_t File::Write(const void* buffer, int64_t num_bytes) {
  ASSERT(handle_->fd() >= 0);
  return TEMP_FAILURE_RETRY(write(handle_->fd(), buffer, num_bytes));
}


off_t File::Position() {
  ASSERT(handle_->fd() >= 0);
  return TEMP_FAILURE_RETRY(lseek(handle_->fd(), 0, SEEK_CUR));
}


bool File::SetPosition(int64_t position) {
  ASSERT(handle_->fd() >= 0);
  return TEMP_FAILURE_RETRY(lseek(handle_->fd(), position, SEEK_SET) != -1);
}


bool File::Truncate(int64_t length) {
  ASSERT(handle_->fd() >= 0);
  return TEMP_FAILURE_RETRY(ftruncate(handle_->fd(), length) != -1);
}


void File::Flush() {
  ASSERT(handle_->fd() >= 0);
  TEMP_FAILURE_RETRY(fsync(handle_->fd()));
}


off_t File::Length() {
  ASSERT(handle_->fd() >= 0);
  off_t position = TEMP_FAILURE_RETRY(lseek(handle_->fd(), 0, SEEK_CUR));
  if (position < 0) {
    // The file is not capable of seeking. Return an error.
    return -1;
  }
  off_t result = TEMP_FAILURE_RETRY(lseek(handle_->fd(), 0, SEEK_END));
  TEMP_FAILURE_RETRY(lseek(handle_->fd(), position, SEEK_SET));
  return result;
}


File* File::Open(const char* name, FileOpenMode mode) {
  int flags = O_RDONLY;
  if ((mode & kWrite) != 0) {
    flags = (O_RDWR | O_CREAT);
  }
  if ((mode & kTruncate) != 0) {
    flags = flags | O_TRUNC;
  }
  int fd = TEMP_FAILURE_RETRY(open(name, flags, 0666));
  if (fd < 0) {
    return NULL;
  }
  return new File(name, new FileHandle(fd));
}


bool File::Exists(const char* name) {
  struct stat st;
  if (TEMP_FAILURE_RETRY(stat(name, &st)) == 0) {
    return S_ISREG(st.st_mode);  // Deal with symlinks?
  } else {
    return false;
  }
}


bool File::Create(const char* name) {
  int fd = TEMP_FAILURE_RETRY(open(name, O_RDONLY | O_CREAT, 0666));
  if (fd < 0) {
    return false;
  }
  return (close(fd) == 0);
}


bool File::Delete(const char* name) {
  int status = TEMP_FAILURE_RETRY(remove(name));
  if (status == -1) {
    return false;
  }
  return true;
}


bool File::IsAbsolutePath(const char* pathname) {
  return (pathname != NULL && pathname[0] == '/');
}


char* File::GetCanonicalPath(const char* pathname) {
  char* abs_path = NULL;
  if (pathname != NULL) {
    do {
      abs_path = realpath(pathname, NULL);
    } while (abs_path == NULL && errno == EINTR);
    assert(abs_path == NULL || IsAbsolutePath(abs_path));
  }
  return abs_path;
}


const char* File::PathSeparator() {
  return "/";
}


const char* File::StringEscapedPathSeparator() {
  return "/";
}
