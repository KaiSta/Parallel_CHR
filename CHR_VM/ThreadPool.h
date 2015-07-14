#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <atomic>
#include <functional>
#include <tbb/concurrent_queue.h>

class ThreadPool;

namespace detail
{
  class Worker {
  public:
    Worker(ThreadPool& s) : pool_(s) { }
    void operator()();
  private:
    ThreadPool& pool_;
  };
}

// the actual thread pool
class ThreadPool {
public:
  ThreadPool(size_t thread_count = (std::thread::hardware_concurrency() * 2));
  void enqueue(std::function<void()> f);
  ~ThreadPool();
  void join();
  void stop();
private:
  friend class detail::Worker;

  // need to keep track of threads so we can join them
  std::vector< std::thread > workers_;

  // the task queue
  tbb::concurrent_queue<std::function<void()> > tasks_;

  // synchronization
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  
  std::condition_variable join_;
  std::atomic<uint64_t> counter_;
  bool stop_;
};