#ifndef HEARTEN_SOCKET_BUFFER_H_
#define HEARTEN_SOCKET_BUFFER_H_

#include <sys/uio.h>
#include <vector>
#include <string>

#include "log/log.h"

namespace hearten {

class Buffer {
  static constexpr size_t kPrepend = 0;
  static constexpr size_t KInitSize = 1024;
public:
  explicit Buffer(size_t initial_size = KInitSize) :
    buffer_(kPrepend + initial_size),
    reader_index_(kPrepend),
    writer_index_(kPrepend) {
    ASSERT(readableBytes() == 0);
    ASSERT(writableBytes() == initial_size);
    ASSERT(prependableBytes() == kPrepend);
  }

  size_t readableBytes() const
  { return writer_index_ - reader_index_; }
  size_t writableBytes() const
  { return buffer_.size() - writer_index_; }
  size_t prependableBytes() const
  { return reader_index_; }
  const char* peek() const
  { return buffer_.data() + reader_index_; }

  std::string retrieve(size_t len) {
    ASSERT(len <= readableBytes());
    std::string ret;
    if (len < readableBytes()) {
      ret.assign(peek(), len);
      reader_index_ += len;
    } else {
      ret.assign(retrieveAll());
    }
    return ret;
  }
  std::string retrieveAll() {
    std::string ret{peek(), readableBytes()};
    reader_index_ = writer_index_ = kPrepend;
    return ret;
  }
  void append(const char* data, size_t len) {
    ensureWritableBytes(len);
    std::copy(data, data + len, buffer_.begin() + writer_index_);
    ASSERT(len <= writableBytes());
    writer_index_ += len;
  }

  ssize_t readFd(int fd) {
    char extrabuf[65536];
    iovec vec[2];
    const size_t writable = writableBytes();
    vec[0].iov_base = buffer_.data() + writer_index_;
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    const ssize_t n = ::readv(fd, vec, 2);
    if (n < 0) {
      ERROR << "Buffer::readFd()";
    } else if (static_cast<size_t>(n) <= writable) {
      writer_index_ += n;
    } else {
      writer_index_ = buffer_.size();
      append(extrabuf, n - writable);
    }
    return n;
  }

private:
  void ensureWritableBytes(size_t len) {
    if (writableBytes() < len) {
      if (writableBytes() + prependableBytes() < len + kPrepend) {
        buffer_.resize(writer_index_ + len);
      } else {
        ASSERT(kPrepend < reader_index_);
        size_t readable = readableBytes();
        std::copy(buffer_.begin() + reader_index_,
                  buffer_.begin() + writer_index_,
                  buffer_.begin() + kPrepend);
        reader_index_ = kPrepend;
        writer_index_ = reader_index_ + readable;
        ASSERT(readable == readableBytes());
      }
    }
    ASSERT(writableBytes() >= len);
  }

  std::vector<char> buffer_;
  size_t reader_index_;
  size_t writer_index_;
};

} // namespace hearten

#endif
