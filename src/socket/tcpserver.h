#ifndef HEARTEN_SOCKET_TCPSERVER_H_
#define HEARTEN_SOCKET_TCPSERVER_H_

#include <functional>
#include <memory>
#include <unordered_map>

#include "util/util.h"
#include "schedule/eventloop.h"
#include "socket/tcpconnection.h"
#include "socket/acceptor.h"

namespace hearten {

class TcpServer : public Noncopyable {
public:
  explicit TcpServer(const IPv4Addr& listen_addr) :
    acceptor_(listen_addr), started_(false) {
    acceptor_.setNewConnetionCallback([this](Socket socket) {
      this->newConnection(std::move(socket));
    });
  }
  ~TcpServer() {
    for (auto && item : connections_map_) {
      item.second->connectionDestroyed();
    }
  }

  void start() {
    INFO << "TcpServer start...";
    if (started_ == false) {
      started_ = true;
      acceptor_.listen();
    }
  }

  void setConnectionCallback(TcpConnection::ConnectionCallback cb)
  { connection_cb_ = std::move(cb); }
  void setMessageCallback(TcpConnection::MessageCallback cb)
  { message_cb_ = std::move(cb); }

private:
  void newConnection(Socket socket) {
    DEBUG << "establish newConnection";

    uint32_t conn_id = Random::get_uint32();
    while (connections_map_.find(conn_id)
        != connections_map_.end()) {
      conn_id = Random::get_uint32();
    }

    auto conn = std::make_shared<TcpConnection>(conn_id, std::move(socket));
    connections_map_[conn_id] = conn;
    conn->setConnectionCallback(connection_cb_);
    conn->setMessageCallback(message_cb_);
    conn->setCloseCallback([this](const TcpConnection::sptr& connection) {
      this->removeConnection(connection);
    });
    conn->connectionEstablished();
  }

  void removeConnection(const TcpConnection::sptr& connection) {
    size_t n = connections_map_.erase(connection->getId());
    ASSERT(n == 1);
    connection->connectionDestroyed();
  }

  using ConnectionMap = std::unordered_map<uint32_t, std::shared_ptr<TcpConnection>>;

  Acceptor acceptor_;
  TcpConnection::ConnectionCallback connection_cb_;
  TcpConnection::MessageCallback message_cb_;
  bool started_;
  ConnectionMap connections_map_;
};

} // namespace hearten

#endif
