#ifndef HEARTEN_IOEVENTLOOP_H_
#define HEARTEN_IOEVENTLOOP_H_

#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include <vector>
#include <unordered_set>

#include "log.h"

namespace hearten {

namespace detail {

class Channel : Noncopyable {
  static constexpr int kNoneEvent = 0;
  static constexpr int kReadEvent = EPOLLIN | EPOLLPRI;
  static constexpr int kWriteEvent = EPOLLOUT;
public:
  Channel(int fd) : fd_(fd), events_(kNoneEvent), new_(true) {
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

  Channel& setReadCallback(std::function<void()> cb)
  { read_cb_  = std::move(cb); return *this; }
  Channel& setWriteCallback(std::function<void()> cb)
  { write_cb_ = std::move(cb); return *this; }
  Channel& setErrorCallback(std::function<void()> cb)
  { error_cb_ = std::move(cb); return *this; }
  Channel& setCloseCallback(std::function<void()> cb)
  { close_cb_ = std::move(cb); return *this; }

  Channel& enableReading() { events_ |= kReadEvent;   return *this; }
  Channel& enableWriting() { events_ |= kWriteEvent;  return *this; }
  Channel& disableWrting() { events_ &= ~kWriteEvent; return *this; }
  Channel& disableAll()    { events_ = kNoneEvent;    return *this; }

  bool isNoneEvent() const { return events_ == kNoneEvent; }
  bool isWriting() const { return events_ & kWriteEvent; }
  int getEvents() const { return events_; }
  int getFd() const { return fd_; }
  bool isNewAndSetOld() { bool ret = new_; new_ = false; return ret; }

private:
  const int fd_;
  int events_;
  bool new_;

  std::function<void()> read_cb_;
  std::function<void()> write_cb_;
  std::function<void()> error_cb_;
  std::function<void()> close_cb_;
};

} // namespace detail

class IOEventLoop : detail::Noncopyable {
  static constexpr size_t kInitSize = 16;
public:
  IOEventLoop() : epfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(kInitSize) {
    ASSERT(epfd_ >= 0);
  }
  ~IOEventLoop() {
    ::close(epfd_);
  }

  void loop() {
    while (true) {
      int count = ::epoll_wait(epfd_, events_.data(),
          static_cast<int>(events_.size()), -1);
      if (count > 0) {
        ASSERT(static_cast<size_t>(count) <= events_.size());
        for (int i = 0; i < count; ++i) {
          auto channel = static_cast<detail::Channel*>(events_[i].data.ptr);
          ASSERT(channels_.find(channel->getFd()) != channels_.end());
          DEBUG << "epoller call channel: " << channel->getFd();
          channel->handleEvent(events_[i].events);
        }
        if (static_cast<size_t>(count) == events_.size())
          events_.resize(events_.size() * 2);
      } else {
        ERROR << "Epoller::epoll";
      }
    }
  }

  detail::Channel& registerChannel(int fd) {
    auto [iter, success] = channels_.try_emplace(fd, fd);
    ASSERT(success);
    return iter->second;
  }

  detail::Channel& searchChannel(int fd) {
    auto iter = channels_.find(fd);
    ASSERT(iter != channels_.end());
    return iter->second;
  }

  void updateChannel(detail::Channel& channel) {
    if (channel.isNewAndSetOld()) {
      // a new channel
      ASSERT(!channel.isNoneEvent());
      updateEpollEvent(EPOLL_CTL_ADD, channel);
      DEBUG << "add channel: " << channel.getFd();
    } else {
      // existed channel
      if (channel.isNoneEvent()) {
        DEBUG << "delete channel: " << channel.getFd();
        updateEpollEvent(EPOLL_CTL_DEL, channel);
        size_t n = channels_.erase(channel.getFd());
        ASSERT(n == 1);
      } else {
        DEBUG << "modify channel: " << channel.getFd();
        updateEpollEvent(EPOLL_CTL_MOD, channel);
      }
    }
  }

private:
  void updateEpollEvent(int operation, detail::Channel& channel) {
    epoll_event ev;
    ::memset(&ev, 0, sizeof(ev));
    ev.events = channel.getEvents();
    ev.data.ptr = &channel;
    int fd = channel.getFd();
    ASSERT(::epoll_ctl(epfd_, operation, fd, &ev) == 0);
  }

  int epfd_;
  std::vector<epoll_event> events_;
  std::unordered_map<int, detail::Channel> channels_;
};

} // namespace hearten

#endif
