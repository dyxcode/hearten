#include <utility>
#include <iostream>
#include <string>
#include <unistd.h>

#include "socket/tcpserver.h"
#include "socket/address.h"
#include "schedule/eventloop.h"
#include "log/log.h"

class EchoServer {
public:
  EchoServer(hearten::EventLoop* loop, const hearten::IPv4Addr& listenaddr) :
    loop_(loop),
    server_(listenaddr)
  {
    server_.setConnectionCallback([this](const hearten::TcpConnection::sptr& conn) { this->onConnection(conn); });
    server_.setMessageCallback([this](const hearten::TcpConnection::sptr& conn, hearten::Buffer* buf) { this->onMessage(conn, buf); });
  }
  void start() { server_.start(); }

private:
  void onConnection(const hearten::TcpConnection::sptr& conn) {
    conn->send("hello\n");
  }
  void onMessage(const hearten::TcpConnection::sptr& conn, hearten::Buffer* buf) {
    std::string msg(buf->retrieveAll());
    std::cout << "msg: " << msg << std::endl;
    if (msg == "exit\n") {
      conn->send("good bye\n");
      conn->shutdown();
    }
    if (msg == "quit\n") {
      loop_->quit();
    }
    conn->send(msg);
  }

  hearten::EventLoop * loop_;
  hearten::TcpServer server_;
};

int main() {
  hearten::EventLoop loop;
  hearten::IPv4Addr listenaddr("0.0.0.0", 8888);
  EchoServer echo(&loop, listenaddr);

  echo.start();
  loop.loop();
  return 0;
}
