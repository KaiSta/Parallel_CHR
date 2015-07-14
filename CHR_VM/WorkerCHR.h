#pragma once

#include "Constraint.h"
#include "Rule.h"
#include "Store.h"
#include "placeholder.h"
#include "ThreadPool.h"

#include <tbb/concurrent_vector.h>
#include <tbb/concurrent_queue.h>
#include <mutex>

enum mode
{
  NONE,
  PESSIMISTIC_RESTART,
  OPTIMISTIC_RESTART,
  OPTIMISTIC_PARTIAL_RESTART
};

struct backoff
{
  const size_t init_;
  const size_t step_;
  const size_t threshold_;
  size_t current_;

  backoff(size_t init = 10, size_t step = 2, size_t threshold = 8000)
    : init_(init), step_(step), threshold_(threshold), current_(init)
  {}
  void operator()()
  {
    for (size_t k = 0; k < current_; ++k)
      std::this_thread::yield();

    current_ *= step_;
    if (current_ > threshold_)
      current_ = threshold_;
  }
  void reset() { current_ = init_; }
};

class WorkerCHR
{
  typedef tbb::concurrent_vector<Constraint<uint64_t>*> bucket;
  typedef tbb::concurrent_hash_map < std::string, bucket > bucketmap;

public:
  WorkerCHR(Store* s, bool use_mpi = true);

  //unsafe don't run concurrently (undefined behavior)
  void add_rule(Rule* r); 

  //uncounted versions, deprecated
  void run_parallel(size_t limit, mode m);
  void add_constraint(Constraint<uint64_t>* c);
  std::tuple<Constraint<uint64_t>*, bool, bool> try_goal_sj(Constraint<uint64_t>* c, size_t thread_num, size_t num_threads);
  std::tuple<Constraint<uint64_t>*, bool, bool> try_goal_sj_opt(Constraint<uint64_t>* c, size_t thread_num, size_t num_threads);
  std::tuple<Constraint<uint64_t>*, bool, bool> try_goal_sj_opt_partial_restart(Constraint<uint64_t>* c, size_t thread_num, size_t num_threads);
  std::vector<Constraint<uint64_t>*> get_result();

  //counted versions, tested version
  void run_parallel2(size_t limit, mode m);
  Store::status* add_constraint(Constraint<placeholder>& c, std::vector<uint64_t> args, Store::modi m = Store::modi::ASYNC);
  std::tuple<Store::status*, bool, bool> try_goal_sj2(Store::status* c, size_t thread_num, size_t num_threads);
  std::tuple<Store::status*, bool, bool> try_goal_sj_opt2(Store::status* c, size_t thread_num, size_t num_threads);
  std::tuple<Store::status*, bool, bool> try_goal_sj_opt_partial_restart2(Store::status* c, size_t thread_num, size_t num_threads);
  std::vector<Constraint<uint64_t>*> get_result2();

  //language extension interface using counted version, tested version
  void async(Constraint<placeholder>& c, std::vector<uint64_t> args, mode m);
  void sync(Constraint<placeholder>& c, std::vector<uint64_t> args, mode m);
  void add_stati_(Store::status* s);
  std::vector<Constraint<uint64_t>*> get();
  void wait();
  bool counted_version;

  //language extension interface using uncounted version, not tested
  void async1(Constraint<placeholder>& c, std::vector<uint64_t> args, mode m);
  void sync1(Constraint<placeholder>& c, std::vector<uint64_t> args, mode m);
  std::vector<Constraint<uint64_t>*> get1();
  void add_c_(Constraint<uint64_t>* c);

  ~WorkerCHR();
private:
  Store* store_;
  tbb::concurrent_vector<Rule*> rules_;
  tbb::concurrent_bounded_queue<Constraint<uint64_t>* > queue_;
  tbb::concurrent_bounded_queue<Store::status*> stati_queue_;

  //std::atomic<unsigned int> constraint_count_;
  std::mutex mtx;

  std::random_device rd;
  std::mt19937 gen;

  enum try_goal_states
  {
    SUCCESS,
    FAILED,
    HANDLING
  };

  //uncounted
  void try_matching(Constraint<uint64_t>* c, MatchChecklist& checklist, size_t thread_num, size_t num_threads);

  //counted
  bool try_matching2(Store::status* c, MatchChecklist& checklist, size_t thread_num, size_t num_threads);


  ThreadPool pool_;
  std::atomic<bool> abort_;
  std::atomic<uint64_t> async_count_;

  std::mutex join_mutex_;
  std::condition_variable join_;

  bool use_mpi_;
  std::atomic<mode> used_mode_;
};

