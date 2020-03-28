#ifndef HEARTEN_SCHEDULE_CHANNEL_H_
#define HEARTEN_SCHEDULE_CHANNEL_H_

#include <sys/epoll.h>
#include <functional>
#include <unordered_set>

#include "util/util.h"
#include "log/log.h"

namespace hearten {

class Channel : public Noncopyable {
  using PureFunc = std::function<void()>;
  static constexpr int kNoneEvent = 0;
  static constexpr int kReadEvent = EPOLLIN | EPOLLPRI;
  static constexpr int kWriteEvent = EPOLLOUT;
  friend class Epoller;
public:
  enum Operate { kEnableRead, kEnableWrite, kDisableWrite, KDisableAll };

  Channel(int fd) : fd_(fd), events_(kNoneEvent) {
    DEBUG << "create channel, fd:" << fd_;
  }

  void handleEvent(int revents) {
    if ((revents & EPOLLHUP) && !(revents & EPOLLIN) && close_cb_)
      close_cb_();
    if ((revents & (EPOLLERR)) && error_cb_)
      error_cb_();
    if ((revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) && read_cb_)
      read_cb_();
    if ((revents & EPOLLOUT) && write_cb_)
      write_cb_();
  }

  void setReadCallback(std::function<void()> cb) { read_cb_ = std::move(cb); }
  void setWriteCallback(std::function<void()> cb) { write_cb_ = std::move(cb); }
  void setErrorCallback(std::function<void()> cb) { error_cb_ = std::move(cb); }
  void setCloseCallback(std::function<void()> cb) { close_cb_ = std::move(cb); }

  int getFd() const { return fd_; }
  void setEvents(uint32_t events) { events_ = events; }
  int getEvents() const { return events_; }
  bool isNoneEvent() const { return events_ == kNoneEvent; }

  bool isWriting() const { return events_ & kWriteEvent; }

private:
  void enableReading() { events_ |= kReadEvent; }
  void enableWriting() { events_ |= kWriteEvent; }
  void disableWrting() { events_ &= ~kWriteEvent; }
  void disableAll() { events_ = kNoneEvent; }

  const int fd_;
  int events_;

  PureFunc read_cb_;
  PureFunc write_cb_;
  PureFunc error_cb_;
  PureFunc close_cb_;
};


} // namespace hearten

namespace std {
  
using Channel = hearten::CR<hearten::Channel*>;

template<> struct hash<hearten::Channel*> {
  size_t operator()(Channel channel) const {
    return std::hash<int>()(channel->getFd());
  }
};

template<> struct equal_to<hearten::Channel*> {
  bool operator()(Channel lhs, Channel rhs) const {
    return lhs->getFd() == rhs->getFd();
  }
};

} // namespace std

#endif
