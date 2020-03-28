#ifndef HEARTEN_SCHEDULE_EVENTLOOP_H_
#define HEARTEN_SCHEDULE_EVENTLOOP_H_

#include <vector>
#include <thread>

#include "schedule/epoller.h"
#include "util/util.h"

namespace hearten {

class EventLoop;
thread_local inline EventLoop* tLoopInThisThread = nullptr;

class EventLoop : public Noncopyable {
public:
  EventLoop() : looping_(false), thread_id_(std::this_thread::get_id()) {
    if (tLoopInThisThread) {
      WARN << "Another EventLoop in this thread";
    } else {
      tLoopInThisThread = this;
    }
  }
  ~EventLoop() {
    ASSERT(looping_ == false);
    tLoopInThisThread = nullptr;
  }

  void loop() {
    ASSERT(looping_ == false);
    assertLoop();
    looping_ = true;

    while (looping_) {
      epoller_.epoll();
    }
  }
  void quit() {
    DEBUG << "eventloop quit";
    looping_ = false;
  }
  void assertLoop() { ASSERT(thread_id_ == std::this_thread::get_id()); }

  void changeChannelEvent(Channel* channel, Channel::Operate operate) {
    epoller_.changeChannelEvent(channel, operate);
  }

  static EventLoop* getEventLoopInThisThread() { return tLoopInThisThread; }

private:
  bool looping_;
  const std::thread::id thread_id_;
  Epoller epoller_;
};

} // namespace hearten

#endif
