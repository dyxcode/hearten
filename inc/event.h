#ifndef HEARTEN_EVENT_H_
#define HEARTEN_EVENT_H_

#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include <vector>
#include <unordered_set>
#include <map>
#include <list>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <condition_variable>

#include "log.h"

namespace hearten {

namespace detail {

class PeriodicTask {
  using Period = typename std::chrono::steady_clock::duration;
  using TimePoint = typename std::chrono::steady_clock::time_point;
public:
  template<typename T>
  PeriodicTask(T&& task, const Period& period, int times)
    : task_(std::forward<T>(task)), period_(period), times_(times) { }

  template<typename T, typename C, typename U>
  void operator()(T&& map_node, C& container, U& u_lock) {
    if (--times_ >= 0) {
      if (times_ != 0) {
        map_node.key() += period_;
        container.insert(std::move(map_node));
      }
      u_lock.unlock();
      task_();
      u_lock.lock();
    }
  }
  TimePoint getEndTime(const TimePoint& execute_time) const {
    return execute_time + period_ * times_;
  }

  PeriodicTask(const PeriodicTask&) = delete;
  PeriodicTask(PeriodicTask&&) = default;

private:
  std::function<void()> task_;
  Period period_;
  int times_;
};

template<typename T>
class HashListQueue {
public:
  void put(const T& key) {
    iter_to_node_[key] = nodes_.emplace(nodes_.end(), key);
  }
  T get() const {
    return nodes_.front();
  }
  void remove(const T& key) {
    nodes_.erase(iter_to_node_[key]);
    iter_to_node_.erase(key);
  }
  bool empty() const {
    return nodes_.empty();
  }
private:
  std::unordered_map<T, typename std::list<T>::iterator> iter_to_node_;
  std::list<T> nodes_;
};

class ThreadPool {
  using TimePoint = typename std::chrono::steady_clock::time_point;
  using Duration = typename std::chrono::steady_clock::duration;
public:
  explicit ThreadPool(size_t thread_num)
    : stop_(false), waiting_for_delay_task_(thread_num), cvs_(thread_num) {
    while (thread_num--) {
      threads_.emplace_back([index = thread_num, this]{
        std::unique_lock<std::mutex> u_lock{mtx_};
        while (true) {
          if (stop_) break;
          if (tasks_.empty() || waiting_for_delay_task_ != threads_.size()) {
            scheduler_.put(index);
            cvs_[index].wait(u_lock);
            scheduler_.remove(index);
          } else {
            auto && execute_time = tasks_.begin()->first;
            if (execute_time <= Clock::now()) {
              auto map_node = tasks_.extract(tasks_.begin());
              if (!tasks_.empty() && !scheduler_.empty())
                cvs_[scheduler_.get()].notify_one();
              PeriodicTask& task = map_node.mapped();
              task(map_node, tasks_, u_lock);
            } else {
              scheduler_.put(index);
              waiting_for_delay_task_ = index;
              cvs_[index].wait_until(u_lock, execute_time);
              waiting_for_delay_task_ = threads_.size();
              scheduler_.remove(index);
            }
          }
        }
      });
    }
  }

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> guard{mtx_};
      stop_ = true;
    }
    for (auto && item : cvs_)
      item.notify_one();
    for (auto && item : threads_)
      item.join();
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template<typename F>
  auto execute(F&& task,
               const TimePoint& execute_time = Clock::now(),
               const Duration& period = Duration::zero(),
               int times = 1) {
    return addTask(execute_time,
                   PeriodicTask(std::forward<F>(task), period, times));
  }

  template<typename F>
  auto execute(F&& task,
               const Duration& delay,
               const Duration& period = Duration::zero(),
               int times = 1) {
    return addTask(Clock::now() + delay,
                   PeriodicTask(std::forward<F>(task), period, times));
  }

private:
  void addTask(const TimePoint& execute_time, PeriodicTask&& task) {
    size_t index;
    typename std::multimap<TimePoint, PeriodicTask>::iterator iter;
    {
      std::lock_guard<std::mutex> guard{mtx_};
      iter = tasks_.emplace(execute_time, std::move(task));
      if (scheduler_.empty())
        index = cvs_.size();
      else
        index = (waiting_for_delay_task_ == cvs_.size() ?
                 scheduler_.get() : waiting_for_delay_task_);
    }
    if (index != cvs_.size())
      cvs_[index].notify_one();
  }

private:
  bool stop_;
  size_t waiting_for_delay_task_;
  std::mutex mtx_;
  std::multimap<TimePoint, PeriodicTask> tasks_;
  std::vector<std::condition_variable> cvs_;
  std::vector<std::thread> threads_;
  detail::HashListQueue<size_t> scheduler_;
};

class Channel : Noncopyable {
public:
  Channel() : isListen(false), events_(0) { }

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

  int getEvents() const { return events_; }
  void setEvents(int flag) { events_ |= flag; }
  void unsetEvents(int flag) { events_ &= ~flag; }
  bool checkEvents(int flag) {
    if (flag == 0) return events_ == 0;
    return (events_ & flag) == flag;
  }

  bool getState() const { return state_; }
  void setState(bool state) { state_ = state; }

private:
  bool state_;
  int events_;
  std::function<void()> read_cb_;
  std::function<void()> write_cb_;
  std::function<void()> error_cb_;
  std::function<void()> close_cb_;
};

template<size_t EventListSize>
class Epoller : Noncopyable {
public:
  Epoller()
    : epfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(EventListSize) {
    ASSERT(epfd_ != -1);
  }
  ~Epoller() {
    ASSERT(::close(epfd_) != -1);
  }

  Channel& get(int fd) { return channels_[fd]; }

  void set(int fd, Channel& channel) {
    if (channel.state()) {
      // check is none event
      if (channel.checkEvents(0)) return;
      channel.setOld();
      updateEpollEvent(EPOLL_CTL_ADD, fd, channel);
    } else {
      // existed channel
      if (channel.checkEvents(0)) {
        channel.set()
        updateEpollEvent(EPOLL_CTL_DEL, fd, channel);
      } else {
        updateEpollEvent(EPOLL_CTL_MOD, fd, channel);
      }
    }
  }

  void updateEpollEvent(int operation, int fd, Channel& channel) {
    epoll_event ev;
    ::memset(&ev, 0, sizeof(ev));
    ev.events = channel.getEvents();
    ev.data.ptr = &channel;
    ASSERT(::epoll_ctl(epfd_, operation, fd, &ev) == 0);
  }

  void setTimeout(int timeout) {
    // get the number of ready events
    int ready_events_number = ::epoll_wait(epfd_, events_.data(),
        static_cast<int>(events_.size()), timeout);
    ASSERT(ready_events_number != -1);

    // really have events to deal with
    if (size_t n = ready_events_number; n > 0) {
      ASSERT(n <= events_.size());
      // execute events callback
      for (int i = 0; i < n; ++i) {
        auto channel = static_cast<detail::Channel*>(events_[i].data.ptr);
        channel->handleEvent(events_[i].events);
      }
      // resizing the eventlist
      if (n == events_.size())
        events_.resize(events_.size() * 2);
      else if (n <= events_.size() / 2 && events_.size() > EventListSize)
        events_.resize(events_.size() / 2);
    }
  }

private:
  int epfd_;
  std::vector<epoll_event> events_;
  std::unordered_map<int, Channel> channels_;
};

template<size_t ThreadNumber>
class Scheduler : Noncopyable {
public:
  Scheduler()
    : delay_wake_up_thread_(ThreadNumber) {
  }
  ~Scheduler() {
    {
      std::lock_guard<std::mutex> guard{mtx_};
      stop_ = true;
    }
    for (auto && item : cvs_)
      item.notify_one();
    for (auto && item : threads_)
      item.join();
  }

private:
  size_t delay_wake_up_thread_;
  std::mutex mtx_;
  std::vector<std::condition_variable> cvs_;
  std::vector<std::thread> threads_;
  HashListQueue<size_t> worker_queue_;
};

} // namespace detail

enum class IO : int {
  // for epoll event and callback
  kRead = EPOLLIN | EPOLLPRI,
  kWrite = EPOLLOUT,
  // only for callback
  kError = 1 << 8,
  kClose = 1 << 9,
  // only for epoll event
  kAll = kRead | kWrite,
  kNone = 0
};

template<size_t EventListSize>
class IOHandle : detail::Noncopyable {
  using Epoller = detail::Epoller<EventListSize>;
public:
  IOHandle(int fd, detail::Channel& channel, Epoller& epoller)
    : fd_(fd), channel_(channel), epoller_(epoller) { }

  void open(IO flag) {
    if (isEpollEvent(flag)) {
      channel_.setEvents(static_cast<int>(flag));
      epoller_.setChannel(channel_);
    }
  }
  void close(IO flag) {
    if (isEpollEvent(flag)) {
      channel_.unsetEvents(static_cast<int>(flag));
      epoller_.setChannel(channel_);
    }
  }
  void check(IO flag) {
    if (isEpollEvent(flag))
      channel_.checkEvents(static_cast<int>(flag));
  }
  void set(IO flag, std::function<void()> cb) {
    switch (flag) {
      case IO::kRead : channel_.setReadCallback(std::move(cb)); break;
      case IO::kWrite : channel_.setWriteCallback(std::move(cb)); break;
      case IO::kError : channel_.setErrorCallback(std::move(cb)); break;
      case IO::kClose : channel_.setCloseCallback(std::move(cb)); break;
      default : break;
    }
  }

private:
  bool isEpollEvent(IO flag) {
    return !(flag == IO::kError || flag == IO::kClose);
  }

  int fd_;
  detail::Channel& channel_;
  Epoller& epoller_;
};

template<size_t ThreadNumber, size_t EventListSize>
class Processor : detail::Noncopyable {
  using TimePoint = typename std::chrono::steady_clock::time_point;
  using Duration = typename std::chrono::steady_clock::duration;

public:
  Processor() {}

  IOHandle<EventListSize> io(int fd) {
    return {epoller.channel(fd)};
  }


private:
  bool stop_;
  size_t waiting_for_delay_task_;
  std::multimap<TimePoint, detail::PeriodicTask> tasks_;
  std::vector<std::thread> threads_;
  detail::Epoller<EventListSize> epoller;
};

} // namespace hearten

#endif
