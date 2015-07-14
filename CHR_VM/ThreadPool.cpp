#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t thread_count) : stop_{ false }, counter_{ 0 }
{
  for (size_t i = 0; i < thread_count; ++i)
    workers_.push_back(std::thread(detail::Worker(*this)));
}

void ThreadPool::enqueue(std::function<void()> f)
{
  counter_.fetch_add(1);
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    tasks_.push(f);
  }
  condition_.notify_one();
}

ThreadPool::~ThreadPool()
{
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_ = true;
  }
  condition_.notify_all();

  for (auto& w : workers_)
  {
    w.join();
  }
}

void ThreadPool::stop()
{
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_ = true;
  }
  condition_.notify_all();
}

void ThreadPool::join()
{
  std::unique_lock<std::mutex> lock(queue_mutex_);
  join_.wait(lock, [&]{return !(counter_ > 0); });
}

namespace detail
{
  void Worker::operator()()
  {
    std::function<void()> task;

    while (true)
    {
      bool acquired_task = false;
      {
        std::unique_lock<std::mutex> lock(pool_.queue_mutex_);

        while (!pool_.stop_ && pool_.tasks_.empty())
        {
          pool_.condition_.wait(lock);
        }

        if (pool_.stop_)
        {
          return;
        }

        acquired_task = pool_.tasks_.try_pop(task);
      }

      if (acquired_task)
        task();
      pool_.counter_.fetch_sub(1);
      pool_.join_.notify_all();
    }
  }
}