#ifndef HEARTEN_SOCKET_ADDRESS_H_
#define HEARTEN_SOCKET_ADDRESS_H_

#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

#include "log/log.h"

namespace hearten {

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

  const sockaddr* get_addr() const {
    return reinterpret_cast<const sockaddr*>(&addr_);
  }
  const socklen_t get_len() const { return sizeof(addr_); }

private:
  sockaddr_in addr_;
};

} // namespace hearten

#endif
