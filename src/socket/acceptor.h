#ifndef HEARTEN_SOCKET_ACCEPTOR_H_
#define HEARTEN_SOCKET_ACCEPTOR_H_

#include <functional>

#include "util/util.h"
#include "socket/socket.h"
#include "schedule/eventloop.h"

namespace hearten {

class Acceptor : public Noncopyable {
public:
  using NewConnectionCallback = std::function<void(Socket)>;

  Acceptor(const IPv4Addr& listen_addr) :
    accept_socket_(AF_INET, SOCK_STREAM, IPPROTO_TCP),
    accept_channel_(accept_socket_.getFd()),
    listenning_(false) {
    accept_socket_.setReUseAddr(true);
    accept_socket_.setNonBlockAndCloseOnExec();
    accept_socket_.bind(listen_addr);
    accept_channel_.setReadCallback([this]{ this->handleRead(); });
  }

  void setNewConnetionCallback(NewConnectionCallback cb)
  { newconnetion_cb_ = std::move(cb); }

  bool isListenning() const { return listenning_; }

  void listen() {
    listenning_ = true;
    accept_socket_.listen();
    EventLoop* loop = EventLoop::getEventLoopInThisThread();
    loop->changeChannelEvent(&accept_channel_, Channel::kEnableRead);
  }

private:
  void handleRead() {
    Socket connect_socket = accept_socket_.accept();
    if (newconnetion_cb_) newconnetion_cb_(std::move(connect_socket));
  }

  Socket accept_socket_;
  Channel accept_channel_;
  bool listenning_;
  NewConnectionCallback newconnetion_cb_;
};

} // namespace hearten

#endif
