#ifndef HEARTEN_SERVERNET_H_
#define HEARTEN_SERVERNET_H_

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>

#include "ioeventloop.h"

namespace hearten {

namespace detail {

class IPv4Addr {
public:
  IPv4Addr(uint32_t IP, uint16_t port) {
    ::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    addr_.sin_addr.s_addr = htonl(IP);
  }
  IPv4Addr(const char* IP, uint16_t port) {
    ::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    ::inet_pton(AF_INET, IP, &addr_.sin_addr);
  }
  explicit IPv4Addr(const int fd) {
    ::memset(&addr_, 0, sizeof(addr_));
    socklen_t socklen = sizeof(addr_);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr_), &socklen) < 0)
      ERROR << "getsockname()";
  }

  const socklen_t get_len() const { return sizeof(addr_); }
  const sockaddr* get_addr() const
  { return reinterpret_cast<const sockaddr*>(&addr_); }

private:
  sockaddr_in addr_;
};

class Socket : Noncopyable {
public:
  Socket(int domain, int type, int protocol) :
    sockfd_(::socket(domain, type, protocol)) {
    ASSERT(sockfd_ != -1);
  }
  explicit Socket(int fd) : sockfd_(fd) {
    ASSERT(sockfd_ != -1);
  }
  Socket(Socket&& socket) : sockfd_(socket.sockfd_) {
    socket.sockfd_ = -1;
  }
  Socket& operator=(Socket&& socket) {
    sockfd_ = socket.sockfd_;
    socket.sockfd_ = -1;
    return *this;
  }
  ~Socket() { if (sockfd_ != -1) ASSERT(::close(sockfd_) == 0); }

  int getFd() const { return sockfd_; }

  void bind(const IPv4Addr& addr)
  { ASSERT(::bind(sockfd_, addr.get_addr(), addr.get_len()) == 0); }
  void listen()
  { ASSERT(::listen(sockfd_, 128) == 0); }
  void connect(const IPv4Addr& addr)
  { ASSERT(::connect(sockfd_, addr.get_addr(), addr.get_len()) == 0); }
  Socket accept() {
    Socket client_socket = Socket(::accept(sockfd_, nullptr, nullptr));
    client_socket.setNonBlockAndCloseOnExec();
    return client_socket;
  }

  Socket& setReUseAddr(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval,
        static_cast<socklen_t>(sizeof(optval)));
    return *this;
  }
  Socket& setNonBlockAndCloseOnExec() {
    int flag = ::fcntl(sockfd_, F_GETFL, 0);
    ASSERT(flag != -1);
    flag |= O_NONBLOCK;
    ::fcntl(sockfd_, F_SETFL, flag);

    flag = ::fcntl(sockfd_, F_GETFD, 0);
    ASSERT(flag != -1);
    flag |= O_CLOEXEC;
    ::fcntl(sockfd_, F_SETFD, flag);
    return *this;
  }

private:
  int sockfd_;
};

class Buffer : Noncopyable  {
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
    if (len < readableBytes()) {
      std::string ret{peek(), len};
      reader_index_ += len;
      return ret;
    }
    return retrieveAll();
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

class Acceptor : Noncopyable {
public:
  using NewConnectionCallback = std::function<void(Socket)>;

  Acceptor(const IPv4Addr& listen_addr, IOEventLoop& loop)
    : loop_(loop), accept_socket_(AF_INET, SOCK_STREAM, IPPROTO_TCP) {
    auto& channel = loop_.registerChannel(accept_socket_.getFd())
                         .setReadCallback([this]{
      if (newconnetion_cb_) newconnetion_cb_(accept_socket_.accept());
    });
    loop_.updateChannel(channel.enableReading());

    accept_socket_.setReUseAddr(true).setNonBlockAndCloseOnExec()
                  .bind(listen_addr);
  }

  void listen() { accept_socket_.listen(); }
  Acceptor& setNewConnetionCallback(NewConnectionCallback cb)
  { newconnetion_cb_ = std::move(cb); return *this; }

private:
  IOEventLoop& loop_;
  Socket accept_socket_;
  NewConnectionCallback newconnetion_cb_;
};

class Connection : Noncopyable {
public:
  class Handle {
  public:
    Handle(Connection& connection) : connection_(connection) { }
    void send(std::string message) { connection_.send(std::move(message)); }
    std::string read() { return connection_.read(); }
    void shutdown() { connection_.shutdown(); }
  private:
    Connection& connection_;
  };

  using ConnectionCallback = std::function<void(Handle)>;
  using MessageCallback = std::function<void(Handle)>;
  using CloseCallback = std::function<void(int)>;

  Connection(Socket socket, IOEventLoop& loop)
    : loop_(loop),
      state_(State::kConnecting),
      socket_(std::move(socket)) {
    loop.registerChannel(socket_.getFd())
        .setReadCallback([this]{ handleRead(); })
        .setWriteCallback([this]{ handleWrite(); })
        .setCloseCallback([this]{ handleClose(); })
        .setErrorCallback([this]{ handleError(); });
  }

  Connection& setConnectionCallback(ConnectionCallback cb)
  { connection_cb_ = std::move(cb); return *this; }
  Connection& setMessageCallback(MessageCallback cb)
  { message_cb_ = std::move(cb); return *this; }
  Connection& setCloseCallback(CloseCallback cb)
  { close_cb_ = std::move(cb); return *this; }

  void connectionEstablished() {
    ASSERT(state_ == State::kConnecting);
    setState(State::kConnected);
    loop_.updateChannel(loop_.searchChannel(socket_.getFd()).enableReading());
    if (connection_cb_) connection_cb_(Handle(*this));
  }

  void connectionDestroyed() {
    ASSERT(state_ == State::kConnected);
    setState(State::kDisconnected);
    auto& channel = loop_.searchChannel(socket_.getFd());
    if (channel.isNoneEvent()) return;
    loop_.updateChannel(channel.disableAll());
  }

private:
  enum class State {
    kConnecting, kConnected, kDisconnecting, kDisconnected
  };

  void setState(State s) { state_ = s; }

  void handleRead() {
    DEBUG << "tcpconnection handleRead(), fd: " << socket_.getFd();
    ssize_t n = input_buffer_.readFd(socket_.getFd());
    if (n > 0) {
      if (message_cb_) message_cb_(Handle(*this));
    } else if (n == 0) {
      handleClose();
    } else {
      handleError();
    }
  }

  void handleWrite() {
    DEBUG << "tcpconnection handleWrite(), fd: " << socket_.getFd();
    auto& channel = loop_.searchChannel(socket_.getFd());
    if (channel.isWriting()) {
      ssize_t n = ::write(channel.getFd(), output_buffer_.peek(), output_buffer_.readableBytes());
      if (n > 0) {
        output_buffer_.retrieve(n);
        if (output_buffer_.readableBytes() == 0) {
          loop_.updateChannel(channel.disableWrting());
          if (state_ == State::kDisconnecting) shutdown();
        } else {
          INFO << "going to write more data";
        }
      } else {
        ERROR << "TcpConnection::handleWrite()";
      }
    } else {
      INFO << "connection is down, no more writing";
    }
  }

  void handleClose() {
    DEBUG << "tcpconnection handleClose()";
    ASSERT(state_ == State::kConnected);
    loop_.updateChannel(loop_.searchChannel(socket_.getFd()).disableAll());
    close_cb_(socket_.getFd());
  }

  void handleError() {
    ERROR << "TcpConnection::handleError()";
  }

  void send(std::string message) {
    auto& channel = loop_.searchChannel(socket_.getFd());
    if (state_ == State::kConnected) {
      ssize_t nwrite = 0;
      if (!channel.isWriting() && output_buffer_.readableBytes() == 0) {
        nwrite = ::write(channel.getFd(), message.data(), message.size());
        if (nwrite >= 0) {
          DEBUG << "write success";
          if (static_cast<size_t>(nwrite) < message.size()) {
            INFO << "going to write more data";
          }
        } else {
          nwrite = 0;
          ASSERT(errno == EWOULDBLOCK);
        }
      }
      ASSERT(nwrite >= 0);
      if (static_cast<size_t>(nwrite) < message.size()) {
        output_buffer_.append(message.data() + nwrite, message.size() - nwrite);
        if (!channel.isWriting()) {
          loop_.updateChannel(channel.enableWriting());
        }
      }
    }
  }

  std::string read() { return input_buffer_.retrieveAll(); }

  void shutdown() {
    if (state_ == State::kConnected) {
      setState(State::kDisconnecting);
      if (!loop_.searchChannel(socket_.getFd()).isWriting()) {
        ASSERT(::shutdown(socket_.getFd(), SHUT_WR) == 0);
        setState(State::kConnected);
        handleClose();
      }
    }
  }

  IOEventLoop& loop_;
  State state_;
  Socket socket_;
  ConnectionCallback connection_cb_;
  MessageCallback message_cb_;
  CloseCallback close_cb_;
  Buffer input_buffer_;
  Buffer output_buffer_;
};

} // namespace detail

class ServerNet : detail::Noncopyable {
  using ConnectionCallback = detail::Connection::ConnectionCallback;
  using MessageCallback = detail::Connection::MessageCallback;
public:
  explicit ServerNet(const detail::IPv4Addr& listen_addr)
    : acceptor_(listen_addr, loop_) {
    acceptor_.setNewConnetionCallback([this](detail::Socket socket) {
        newConnection(std::move(socket));
    });
  }
  ~ServerNet() {
    for (auto && item : connections_) {
      item.second.connectionDestroyed();
    }
  }

  void start() {
    INFO << "TcpServer start...";
    acceptor_.listen();
    loop_.loop();
  }

  ServerNet& setConnectionCallback(ConnectionCallback cb)
  { connection_cb_ = std::move(cb); return *this; }
  ServerNet& setMessageCallback(MessageCallback cb)
  { message_cb_ = std::move(cb); return *this; }

private:
  void newConnection(detail::Socket socket) {
    DEBUG << "establish newConnection, fd: " << socket.getFd();

    auto [iter, success] = connections_.try_emplace(socket.getFd(),
                                          std::move(socket), loop_);
    ASSERT(success);
    iter->second.setConnectionCallback(connection_cb_)
                .setMessageCallback(message_cb_)
                .setCloseCallback([this](int fd) { connections_.erase(fd); })
                .connectionEstablished();
  }

  IOEventLoop loop_;
  detail::Acceptor acceptor_;
  std::unordered_map<int, detail::Connection> connections_;
  ConnectionCallback connection_cb_;
  MessageCallback message_cb_;
};

} // namespace hearten

#endif
