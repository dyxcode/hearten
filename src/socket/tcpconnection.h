#ifndef HEARTEN_SOCKET_TCPCONNECTION_H_
#define HEARTEN_SOCKET_TCPCONNECTION_H_

#include <functional>
#include <string>
#include <memory>

#include "schedule/eventloop.h"
#include "socket/buffer.h"
#include "socket/socket.h"
#include "util/util.h"

namespace hearten {

class TcpConnection : public Noncopyable, public std::enable_shared_from_this<TcpConnection> {
public:
  using sptr = std::shared_ptr<TcpConnection>;
  using ConnectionCallback = std::function<void(const sptr&)>;
  using MessageCallback = std::function<void(const sptr&, Buffer*)>;
  using CloseCallback = std::function<void(const sptr&)>;

  TcpConnection(uint32_t id, Socket socket) :
    id_(id),
    state_(State::kConnecting),
    socket_(std::move(socket)),
    channel_(socket_.getFd())
  {
    channel_.setReadCallback([this]{ this->handleRead(); });
    channel_.setWriteCallback([this]{ this->handleWrite(); });
    channel_.setCloseCallback([this]{ this->handleClose(); });
    channel_.setErrorCallback([this]{ this->handleError(); });
    DEBUG << "tcpconnection finish with setting channel callback";
  }

  void setConnectionCallback(ConnectionCallback cb)
  { connection_cb_ = std::move(cb); }
  void setMessageCallback(MessageCallback cb)
  { message_cb_ = std::move(cb); }
  void setCloseCallback(CloseCallback cb)
  { close_cb_ = std::move(cb); }

  void send(const std::string& message) {
    if (state_ == State::kConnected) {
      ssize_t nwrite = 0;
      if (!channel_.isWriting() && output_buffer_.readableBytes() == 0) {
        nwrite = ::write(channel_.getFd(), message.data(), message.size());
        if (nwrite >= 0) {
          if (static_cast<size_t>(nwrite) < message.size()) {
            INFO << "going to write more data";
          }
        } else {
          nwrite = 0;
          if (errno != EWOULDBLOCK) {
            ERROR << "TcpConnection::send()";
          }
        }
      }
      ASSERT(nwrite >= 0);
      if (static_cast<size_t>(nwrite) < message.size()) {
        output_buffer_.append(message.data() + nwrite, message.size() - nwrite);
        if (!channel_.isWriting()) {
          EventLoop* loop = EventLoop::getEventLoopInThisThread();
          loop->changeChannelEvent(&channel_, Channel::kEnableWrite);
        }
      }
    }
  }

  void shutdown() {
    if (state_ == State::kConnected) {
      setState(State::kDisconnecting);
      if (channel_.isWriting()) {
        if (::shutdown(socket_.getFd(), SHUT_WR) < 0) {
          ERROR << "TcpConnection::shutdown()";
        }
      }
    }
  }

  void connectionEstablished() {
    ASSERT(state_ == State::kConnecting);
    setState(State::kConnected);
    EventLoop* loop = EventLoop::getEventLoopInThisThread();
    loop->changeChannelEvent(&channel_, Channel::kEnableRead);
    connection_cb_(shared_from_this());
  }

  void connectionDestroyed() {
    DEBUG << "connection destroyed";
    ASSERT(state_ == State::kConnected);
    setState(State::kDisconnected);
    if (channel_.isNoneEvent()) return;
    EventLoop* loop = EventLoop::getEventLoopInThisThread();
    loop->changeChannelEvent(&channel_, Channel::KDisableAll);
  }

  uint32_t getId() const { return id_; }

private:
  enum class State {
    kConnecting, kConnected, kDisconnecting, kDisconnected
  };

  void setState(State s) { state_ = s; }

  void handleRead() {
    DEBUG << "tcpconnection handleRead()";
    ssize_t n = input_buffer_.readFd(channel_.getFd());
    if (n > 0) {
      message_cb_(shared_from_this(), &input_buffer_);
    } else if (n == 0) {
      handleClose();
    } else {
      handleError();
    }
  }

  void handleWrite() {
    DEBUG << "tcpconnection handleWrite()";
    if (channel_.isWriting()) {
      ssize_t n = ::write(channel_.getFd(), output_buffer_.peek(), output_buffer_.readableBytes());
      if (n > 0) {
        output_buffer_.retrieve(n);
        if (output_buffer_.readableBytes() == 0) {
          EventLoop* loop = EventLoop::getEventLoopInThisThread();
          loop->changeChannelEvent(&channel_, Channel::kDisableWrite);
          if (state_ == State::kDisconnecting) {
            shutdown();
          }
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
    EventLoop* loop = EventLoop::getEventLoopInThisThread();
    loop->changeChannelEvent(&channel_, Channel::KDisableAll);
    close_cb_(shared_from_this());
  }

  void handleError() {
    ERROR << "TcpConnection::handleError()";
  }

  uint32_t id_;
  State state_;
  Socket socket_;
  Channel channel_;
  ConnectionCallback connection_cb_;
  MessageCallback message_cb_;
  CloseCallback close_cb_;
  Buffer input_buffer_;
  Buffer output_buffer_;
};

} // namespace hearten

#endif
