#ifndef HEARTEN_SOCKET_SOCKET_H_
#define HEARTEN_SOCKET_SOCKET_H_

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "socket/address.h"
#include "log/log.h"

namespace hearten {

class Socket : public Noncopyable {
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

  void bind(const IPv4Addr& addr) {
    ASSERT(::bind(sockfd_, addr.get_addr(), addr.get_len()) == 0);
  }
  void listen() { ASSERT(::listen(sockfd_, 128) == 0); }
  void connect(const IPv4Addr& addr) {
    ASSERT(::connect(sockfd_, addr.get_addr(), addr.get_len()) == 0);
  }
  Socket accept() {
    Socket client_socket = Socket(::accept(sockfd_, nullptr, nullptr));
    client_socket.setNonBlockAndCloseOnExec();
    return client_socket;
  }

  void setReUseAddr(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval,
        static_cast<socklen_t>(sizeof(optval)));
  }
  void setNonBlockAndCloseOnExec() {
    int flag = ::fcntl(sockfd_, F_GETFL, 0);
    ASSERT(flag != -1);
    flag |= O_NONBLOCK;
    ::fcntl(sockfd_, F_SETFL, flag);

    flag = ::fcntl(sockfd_, F_GETFD, 0);
    ASSERT(flag != -1);
    flag |= O_CLOEXEC;
    ::fcntl(sockfd_, F_SETFD, flag);
  }

private:
  int sockfd_;
};

} // namespace hearten

#endif
