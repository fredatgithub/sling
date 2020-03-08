// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/stream/buffer.h"

#include <string.h>

#include "sling/base/logging.h"

namespace sling {

void Buffer::Reset(size_t size) {
  if (size != capacity()) {
    if (size == 0) {
      free(floor_);
      floor_ = ceil_ = begin_ = end_ = nullptr;
    } else {
      floor_ = static_cast<char *>(realloc(floor_, size));
      CHECK(floor_ != nullptr) << "Out of memory, " << size << " bytes";
      ceil_ = floor_ + size;
    }
  }
  begin_ = end_ = floor_;
}

void Buffer::Resize(size_t size) {
  if (size != capacity()) {
    size_t offset = begin_ - floor_;
    size_t used = end_ - begin_;
    floor_ = static_cast<char *>(realloc(floor_, size));
    CHECK(floor_ != nullptr) << "Out of memory, " << size << " bytes";
    ceil_ = floor_ + size;
    begin_ = floor_ + offset;
    end_ = begin_ + used;
  }
}

void Buffer::Flush() {
  if (begin_ > floor_) {
    size_t used = end_ - begin_;
    memmove(floor_, begin_, used);
    begin_ = floor_;
    end_ = begin_ + used;
  }
}

void Buffer::Ensure(size_t size) {
  size_t minsize = end_ - floor_ + size;
  size_t newsize = ceil_ - floor_;
  if (newsize == 0) newsize = 4096;
  while (newsize < minsize) newsize *= 2;
  Resize(newsize);
}

void Buffer::Clear() {
  free(floor_);
  floor_ = ceil_ = begin_ = end_ = nullptr;
}

char *Buffer::Append(size_t size) {
  Ensure(size);
  char *data = end_;
  end_ += size;
  return data;
}

char *Buffer::Consume(size_t size) {
  DCHECK_LE(size, available());
  char *data = begin_;
  begin_ += size;
  return data;
}

void Buffer::Read(void *data, size_t size) {
  CHECK_LE(size, available());
  memcpy(data, begin_, size);
  begin_ += size;
}

void Buffer::Write(const void *data, size_t size) {
  Ensure(size);
  memcpy(end_, data, size);
  end_ += size;
}

}  // namespace sling
