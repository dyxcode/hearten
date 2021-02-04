#ifndef HEARTEN_EVENT_H_
#define HEARTEN_EVENT_H_

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
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

// for IOHandle
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

namespace detail {

// speed up the queue
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

template<typename Clock>
class Task {
public:
  template<typename T>
  Task(T&& task, const typename Clock::duration& period, int times)
    : task_(std::forward<T>(task)), period_(period), times_(times) { }

  template<typename T, typename C, typename U>
  void operator()(T&& map_node, C& container,
      std::unique_lock<std::mutex>& u_lock) {
    // need to execute
    if (--times_ >= 0) {
      // there are execution times left
      if (times_ != 0) {
        map_node.key() += period_;
        container.insert(std::forward<T>(map_node));
      }
      u_lock.unlock();
      task_();
      u_lock.lock();
    }
  }
  auto getEndTime(const typename Clock::time_point& execute_time) const {
    return execute_time + period_ * times_;
  }

  Task(const Task&) = delete;
  Task(Task&&) = default;

private:
  std::function<void()> task_;
  typename Clock::duration period_;
  int times_;
};

template<typename Clock, size_t ThreadNumber>
class Scheduler : Noncopyable {
  using TimePoint = typename Clock::time_point;
  using Duration = typename Clock::duration;
  using Task = Task<Clock>;
  using Iter = typename std::multimap<TimePoint, Task>::iterator;
public:
  Scheduler()
    : stop_(true),
      delay_wake_up_thread_(ThreadNumber),
      cvs_(ThreadNumber) { }

  ~Scheduler() {
    closeAllThreads();
  }

  void start() {
    {
      std::lock_guard<std::mutex> guard{mtx_};
      if (!stop_) return;
      stop_ = false;
    }
    for (size_t i = 0; i != ThreadNumber; ++i) {
      threads_.emplace_back([i, this]{
        std::unique_lock<std::mutex> u_lock{mtx_};
        while (true) {
          if (stop_) break;
          // no task or there is already a thread waiting to delay
          if (tasks_.empty() || delay_wake_up_thread_ != ThreadNumber) {
            // put this thread into waiting order
            orders_.put(i);
            cvs_[i].wait(u_lock);
            // remove the thread from waiting order on wake up
            orders_.remove(i);
          } else {
            auto && execute_time = tasks_.begin()->first;
            if (execute_time <= Clock::now()) {
              auto map_node = tasks_.extract(tasks_.begin());
              // there are also tasks and waiting thread
              if (!tasks_.empty() && !orders_.empty())
                cvs_[orders_.get()].notify_one();
              auto && task = map_node.mapped();
              task(map_node, tasks_, u_lock);
            } else {
              // wait for delay task
              orders_.put(i);
              delay_wake_up_thread_ = i;
              cvs_[i].wait_until(u_lock, execute_time);
              delay_wake_up_thread_ = ThreadNumber;
              orders_.remove(i);
            }
          }
        }
      });
    }
  }
  void stop() {
    closeAllThreads();
    threads_.clear();
    tasks_.clear();
  }

  template<typename Iter>
  void submit(Iter begin, Iter end) {
    size_t wake_up_thread;
    {
      std::lock_guard<std::mutex> guard{mtx_};
      std::for_each(begin, end, [this](auto && f) {
        tasks_.emplace(Clock::now(), f);
      });
      wake_up_thread = getWaitingThread();
    }
    wakeUpThread(wake_up_thread);
  }

  template<typename F>
  auto execute(F&& task, const TimePoint& execute_time,
                const Duration& period, int times) {
    size_t wake_up_thread;
    // save the iterator to cancel the task
    Iter iter;
    {
      std::lock_guard<std::mutex> guard{mtx_};
      iter = tasks_.emplace(execute_time, std::forward<F>(task));
      wake_up_thread = getWaitingThread();
    }
    wakeUpThread(wake_up_thread);
    return iter;
  }

  void cancel(Iter iter) {
    std::unique_lock<std::mutex> u_lock{mtx_};
    size_t i = delay_wake_up_thread_;
    // if cancel the task of the thread which is waiting for delay
    bool need_notify = (iter == tasks_.begin()) && (i != ThreadNumber);
    tasks_.erase(iter);
    u_lock.unlock();
    if (need_notify)
      cvs_[i].notify_one();
  }

private:
  // need to be under mutex
  size_t getWaitingThread() {
    // all threads are working
    if (orders_.empty())
      return ThreadNumber;
    // no thread waiting for delay task
    else if (delay_wake_up_thread_ == ThreadNumber)
      return orders_.get();
    // there is a thread waiting for delay task
    else
      return delay_wake_up_thread_;
  }

  // don't need to be under mutex
  void wakeUpThread(size_t wake_up_thread) {
    if (wake_up_thread != ThreadNumber)
      cvs_[wake_up_thread].notify_one();
  }

  void closeAllThreads() {
    {
      std::lock_guard<std::mutex> guard{mtx_};
      if (stop_) return;
      stop_ = true;
    }
    for (auto && item : cvs_)
      item.notify_one();
    for (auto && item : threads_)
      item.join();
  }

  bool stop_;
  size_t delay_wake_up_thread_;
  std::mutex mtx_;
  std::vector<std::condition_variable> cvs_;
  std::vector<std::thread> threads_;
  std::multimap<TimePoint, Task> tasks_;
  HashListQueue<size_t> orders_;
};

template<typename Clock, size_t ThreadNumber>
class TimerHandle : Noncopyable {
  using Scheduler = Scheduler<Clock, ThreadNumber>;
  using TimePoint = typename Clock::time_point;
  using Iter = typename std::multimap<TimePoint, Task<Clock>>::iterator;
public:
  TimerHandle(Scheduler& scheduler, Iter iter)
    : scheduler_(scheduler), task_iter_(iter),
      end_time_(iter->second.getEndTime(iter->first)){ }

  void cancel() {
    if (done()) return;
    scheduler_.cancel(task_iter_);
    end_time_ = Clock::now();
  }
  void done() {
    return end_time_ <= Clock::now();
  }

private:
  Scheduler& scheduler_;
  Iter task_iter_;
  TimePoint end_time_;
};

template<typename Scheduler>
class SignalHandle : Noncopyable {
public:
  template<typename... F>
  SignalHandle(Scheduler& scheduler, F&& ... callbacks)
    : scheduler_(scheduler) {
    (..., callbacks_.push_back(std::forward<F>(callbacks)));
  }

  void operator()() const {
    scheduler_.submit(callbacks_.cbegin(), callbacks_.cend());
  }

private:
  Scheduler& scheduler_;
  std::vector<std::function<void()>> callbacks_;
};

class Channel : Noncopyable {
public:
  Channel() : events_(0) { }

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

private:
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
      wakeup_fd_(-1),
      events_(EventListSize) {
    ASSERT(epfd_ != -1);
  }
  ~Epoller() {
    ASSERT(::close(epfd_) != -1);
  }

  void monitor(int fd, Channel& channel) {
    auto iter = fds_.find(fd);
    if (iter == fds_.end()) {
      // check is none event
      if (channel.checkEvents(0)) return;
      fds_.emplace(fd);
      updateEpollEvent(EPOLL_CTL_ADD, fd, channel);
    } else {
      // existed channel
      if (channel.checkEvents(0)) {
        fds_.emplace(iter);
        updateEpollEvent(EPOLL_CTL_DEL, fd, channel);
      } else {
        updateEpollEvent(EPOLL_CTL_MOD, fd, channel);
      }
    }
  }

  void epoll() {
    if (wakeup_fd_ != -1) return;
    setWakeupConfig();
    while (true) {
      // get the number of ready events
      int ready_events_number = ::epoll_wait(epfd_, events_.data(),
          static_cast<int>(events_.size()), -1);
      ASSERT(ready_events_number != -1);

      // really have events to deal with
      if (size_t n = ready_events_number; n > 0) {
        ASSERT(n <= events_.size());
        // execute events callback
        for (int i = 0; i < n; ++i) {
          auto channel = static_cast<Channel*>(events_[i].data.ptr);
          // check wakeup channel
          if (channel == &wakeup_channel_) {
            unsetWakeupConfig();
            return;
          }
          channel->handleEvent(events_[i].events);
        }
        // resizing the eventlist
        if (n == events_.size())
          events_.resize(events_.size() * 2);
        else if (n <= events_.size() / 2 && events_.size() > EventListSize)
          events_.resize(events_.size() / 2);
      }
    }
  }

  void wakeup() {
    if (wakeup_fd_ == -1) return;
    uint64_t data = 1;
    ASSERT(sizeof(data) == write(wakeup_fd_, &data, sizeof(data)));
  }

private:
  // add(delete, modify) the events into epoll
  void updateEpollEvent(int operation, int fd, Channel& channel) {
    epoll_event ev;
    ::memset(&ev, 0, sizeof(ev));
    ev.events = channel.getEvents();
    ev.data.ptr = &channel;
    ASSERT(::epoll_ctl(epfd_, operation, fd, &ev) == 0);
  }

  void setWakeupConfig() {
    wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(wakeup_fd_ != -1);
    updateEpollEvent(EPOLL_CTL_ADD, wakeup_fd_,
        wakeup_channel_.setEvents(static_cast<int>(IO::kRead)));
  }

  void unsetWakeupConfig() {
    updateEpollEvent(EPOLL_CTL_DEL, wakeup_fd_,
        wakeup_channel_.unsetEvents(static_cast<int>(IO::kRead)));
    ASSERT(::close(wakeup_fd_) != -1);
    wakeup_fd_ = -1;
  }

  int epfd_;
  int wakeup_fd_;
  Channel wakeup_channel_;
  std::unordered_set<int> fds_;
  std::vector<epoll_event> events_;
};

template<size_t EventListSize>
class IOHandle : Noncopyable {
public:
  IOHandle(int fd, Epoller<EventListSize>& epoller) : fd_(fd), epoller_(epoller) { }

  ~IOHandle() {
    close<IO::kAll>();
  }

  template<IO flag> void open() {
    if constexpr (isEpollEvent(flag)) {
      channel_.setEvents(static_cast<int>(flag));
      epoller_.monitor(fd_, channel_);
    }
  }
  template<IO flag> void close() {
    if constexpr (isEpollEvent(flag)) {
      channel_.unsetEvents(static_cast<int>(flag));
      epoller_.monitor(fd_, channel_);
    }
  }
  template<IO flag> void check() {
    if constexpr (isEpollEvent(flag))
      channel_.checkEvents(static_cast<int>(flag));
  }

  // dispatch different kinds of callbacks
  template<IO flag> void set(std::function<void()> cb) { }

  template<> void set<IO::kRead>(std::function<void()> cb) {
    channel_.setReadCallback(std::move(cb));
  }
  template<> void set<IO::kWrite>(std::function<void()> cb) {
    channel_.setWriteCallback(std::move(cb));
  }
  template<> void set<IO::kError>(std::function<void()> cb) {
    channel_.setErrorCallback(std::move(cb));
  }
  template<> void set<IO::kClose>(std::function<void()> cb) {
    channel_.setCloseCallback(std::move(cb));
  }

private:
  // distinguish epoll events from callbacks
  constexpr bool isEpollEvent(IO flag) {
    return !(flag == IO::kError || flag == IO::kClose);
  }

  int fd_;
  Channel channel_;
  Epoller<EventListSize>& epoller_;
};

} // namespace detail

template<typename Clock, size_t ThreadNumber, size_t EventListSize>
class Processor : detail::Noncopyable {
  using TimePoint = typename Clock::time_point;
  using Duration = typename Clock::duration;
  using Epoller = detail::Epoller<EventListSize>;
  using Scheduler = detail::Scheduler<Clock, ThreadNumber>;
public:
  using IOhandle = detail::IOHandle<EventListSize>;
  using TimerHandle = detail::TimerHandle<Clock, ThreadNumber>;
  using SignalHandle = detail::SignalHandle<Scheduler>;

  Processor() {}

  auto io(int fd) {
    return detail::IOHandle(fd, epoller_);
  }

  template<typename F>
  auto timer(F&& task,
          const TimePoint& execute_time = Clock::now(),
          const Duration& period = Duration::zero(),
          int times = 1) {
    auto iter = scheduler_.execute(
        std::forward<F>(task), execute_time, period, times);
    return detail::TimerHandle(scheduler_, iter);
  }

  template<typename... F>
  auto signal(F&& ... callbacks) {
    return detail::SignalHandle(scheduler_, std::forward<F>(callbacks)...);
  }

  void run() {
    scheduler_.start();
    epoller_.epoll();
  }
  void shutdown() {
    epoller_.wakeup();
    scheduler_.stop();
  }

private:
  Scheduler scheduler_;
  Epoller epoller_;
};

} // namespace hearten

#endif
