#ifndef HEARTEN_SCHEDULE_EPOLLER_H_
#define HEARTEN_SCHEDULE_EPOLLER_H_

#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include <unordered_set>

#include "schedule/channel.h"
#include "util/util.h"
#include "log/log.h"

namespace hearten {

class Epoller : public Noncopyable {
  static constexpr size_t kInitSize = 16;
public:
  Epoller() : epfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(kInitSize) {
    ASSERT(epfd_ >= 0);
  }
  ~Epoller() { ::close(epfd_); }

  void epoll() {
    int count = ::epoll_wait(epfd_, events_.data(),
        static_cast<int>(events_.size()), 50);
    if (count == 0) return;
    if (count > 0) {
      ASSERT(static_cast<size_t>(count) <= events_.size());
      for (int i = 0; i < count; ++i) {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        ASSERT(channels_.find(channel) != channels_.end());
        DEBUG << "epoller call channel: " << channel->getFd();
        channel->handleEvent(events_[i].events);
      }
      if (static_cast<size_t>(count) == events_.size())
        events_.resize(events_.size() * 2);
    } else {
      ERROR << "Epoller::epoll";
    }
  }

  void changeChannelEvent(Channel* channel, Channel::Operate operate) {
    if (operate == Channel::kEnableRead) channel->enableReading();
    if (operate == Channel::kEnableWrite) channel->enableWriting();
    if (operate == Channel::kDisableWrite) channel->disableWrting();
    if (operate == Channel::KDisableAll) channel->disableAll();
    updateChannel(channel);
  }


private:
  void updateChannel(Channel* channel) {
    if (channels_.find(channel) == channels_.end()) {
      // a new channel
      ASSERT(!channel->isNoneEvent());
      channels_.insert(channel);
      updateEpollEvent(EPOLL_CTL_ADD, channel);
      DEBUG << "add channel: " << channel->getFd();
    } else {
      // existed channel
      if (channel->isNoneEvent()) {
        size_t n = channels_.erase(channel);
        ASSERT(n == 1);
        DEBUG << "delete channel: " << channel->getFd();
        updateEpollEvent(EPOLL_CTL_DEL, channel);
      } else {
        updateEpollEvent(EPOLL_CTL_MOD, channel);
      }
    }
  }

  void updateEpollEvent(int operation, Channel* channel) {
    epoll_event ev;
    ::memset(&ev, 0, sizeof(ev));
    ev.events = channel->getEvents();
    ev.data.ptr = channel;
    int fd = channel->getFd();
    if (::epoll_ctl(epfd_, operation, fd, &ev) < 0) {
      if (operation == EPOLL_CTL_DEL)
        ERROR << "epoll_ctl op = EPOLL_CTL_DEL";
      else if (operation == EPOLL_CTL_MOD)
        ERROR << "epoll_ctl op = EPOLL_CTL_MOD";
      else if (operation == EPOLL_CTL_ADD)
        ERROR << "epoll_ctl op = EPOLL_CTL_ADD";
      else
        ERROR << "epoll_ctl op = unknown";
    }
  }

  int epfd_;
  std::vector<epoll_event> events_;
  std::unordered_set<Channel*> channels_;
};

} // namespace hearten

#endif
