#pragma once 

#include <tbb/concurrent_vector.h>
#include <vector>
#include <tbb/concurrent_hash_map.h>
#include <string>
#include <atomic>
#include <iostream>
#include <initializer_list>
#include <unordered_set>
#include <unordered_map>
#include <condition_variable>
#include "Constraint.h"
#include <tbb/concurrent_queue.h>

#include <mutex>

class Store
{
  friend class WorkerCHR;

  typedef std::vector<Constraint<uint64_t>*> store_item;
  typedef tbb::concurrent_hash_map<uint64_t, store_item > store_map;
                  //name
  typedef tbb::concurrent_hash_map<uint64_t, store_map > store_map2;


public:
  enum modi
  {
    ASYNC,
    SYNC
  };

  struct status
  {
    status(Constraint<uint64_t>* c, modi mod = ASYNC) : constraint{ c }, stat{ 1 }, m{ mod }
    {}

    bool try_claim(bool& retry)
    {
      uint64_t num     = 0;
      uint64_t pending = 0;
      uint64_t claimed = 0;
      bool ret = false;

      do
      {
        ret = false;
        num = stat.load();
        pending = num & 0xFFFFFFFF;
        claimed = (num & 0xFFFFFFFF00000000) >> 32;

        retry = (claimed > 0);

        if (pending > 0)
        {
          --pending;
          ++claimed;
          ret = true;
        }
        else //necessary?
          break;

      } while (!stat.compare_exchange_strong(num, ((claimed << 32) | pending)));

      return ret;
      /*std::lock_guard<std::mutex> g(mtx); 

      if (claimed > 0 || pending > 0)
        retry = true;

      if (pending > 0)
      {
        --pending;
        ++claimed;
        retry = false;
        return true;
      }
      
      return false;*/
    }

    void consume()
    {
      uint64_t num = stat.load();
      uint64_t pending = num & 0xFFFFFFFF;
      uint64_t claimed = (num & 0xFFFFFFFF00000000) >> 32;

      do
      {
        num = stat.load();
        pending = num & 0xFFFFFFFF;
        claimed = (num & 0xFFFFFFFF00000000) >> 32;
        if (claimed > 0)
        {
          --claimed;
        }
      } while (!stat.compare_exchange_strong(num, ((claimed << 32) | pending)));

      /*std::lock_guard<std::mutex> g(mtx);
      --claimed;
      ++consumed;*/
    }

    void rollback()
    {
      uint64_t num = stat.load();
      uint64_t pending = num & 0xFFFFFFFF;
      uint64_t claimed = (num & 0xFFFFFFFF00000000) >> 32;

      do
      {
        num = stat.load();
        pending = num & 0xFFFFFFFF;
        claimed = (num & 0xFFFFFFFF00000000) >> 32;

        if (claimed > 0)
        {
          --claimed;
          ++pending;
        }
        else
          break;
        
      } while (!stat.compare_exchange_strong(num, ((claimed << 32) | pending)));

      /*std::lock_guard<std::mutex> g(mtx);
      --claimed;
      ++pending;*/
    }

    void add_pending()
    {
      uint64_t num = stat.load();
      uint64_t pending = num & 0xFFFFFFFF;
      uint64_t claimed = (num & 0xFFFFFFFF00000000) >> 32;

      do
      {
        num = stat.load();
        pending = num & 0xFFFFFFFF;
        claimed = (num & 0xFFFFFFFF00000000) >> 32;
        ++pending;
      } while (!stat.compare_exchange_strong(num, ((claimed << 32) | pending)));

      /*std::lock_guard<std::mutex> g(mtx);
      ++pending;*/
    }

    uint64_t num_pendings()
    {
      uint64_t num = stat.load();
      return num & 0xFFFFFFFF;

      //return pending.load();
    }

    uint64_t num_claims()
    {
      uint64_t num = stat.load();
      return (num & 0xFFFFFFFF00000000) >> 32;
      /*std::lock_guard<std::mutex> g(mtx);
      return claimed.load();*/
    }
    uint64_t pendings_and_claims()
    {
      uint64_t num = stat.load();
      uint64_t pending = num & 0xFFFFFFFF;
      uint64_t claimed = (num & 0xFFFFFFFF00000000) >> 32;
      return pending + claimed;
    }

    //std::mutex mtx;
    Constraint<uint64_t>* constraint;
    /*std::atomic<uint32_t> pending;
    std::atomic<uint32_t> claimed;
    std::atomic<uint32_t> consumed;*/

    std::atomic<uint64_t> stat;

   // tbb::concurrent_queue<int> consumed_tokens;
    tbb::concurrent_bounded_queue<int> consumed_tokens;
    modi m;
  };

  typedef std::vector<status*> vec_names;
  typedef tbb::concurrent_hash_map<uint64_t, vec_names> map_second_level;
  typedef tbb::concurrent_hash_map<uint64_t, map_second_level> map_first_level;


  Store(std::vector<Constraint<uint64_t>* >& v);
  Store();
  
  Store(std::initializer_list<Constraint<uint64_t>*> l);
  ~Store();
  void insert(Constraint<uint64_t>* c);

  status* insert_counted(Constraint<placeholder>& pc, std::vector<uint64_t> args, modi m = ASYNC);

  void rehash();

  void print()
  {
    std::unordered_set<Constraint<uint64_t>*> cs;

    for (auto& e : store2_)
    {
      for (auto& i : e.second)
      {
        for (auto& j : i.second)
        {
          if (j->get_state() != CONSUMED && j->get_state() != CLAIMED)
          {
            cs.insert(j);
            //printf("Rulename = %lld, value = %s, id = %i\n", j->name_, j->str().c_str(), j->id_);
            /*std::cout << "Rulename: " << j->name_ << std::endl;
            std::cout << "value = " << j->str() << std::endl;*/
          }
        }
      }
    }

    for (auto& j : cs)
    {
      printf("Rulename = %ui, value = %s, id = %i\n", j->name_, j->str().c_str(), j->id_);
    }
  }

  void print2()
  {
    for (auto& e : name_hashmap_)
    {
      for (auto& j : e.second)
      {
        if (j->num_pendings() > 0)
        {
          printf("PENDING Rulename = %i, value = %s, id = %i, pending = %lu, claimed = %lu\n", j->constraint->name_, 
            j->constraint->str().c_str(), j->constraint->id_, j->num_pendings(), j->num_claims());
        }
        else if (j->num_claims() > 0)
        {
          printf("CLAIMED Rulename = %i, value = %s, id = %i, pending = %lu, claimed = %lu\n", j->constraint->name_,
            j->constraint->str().c_str(), j->constraint->id_, j->num_pendings(), j->num_claims());
        }
      }
    }
  }

  void print_sizes();
private:
  //uncounted version
  store_map store_;
  store_map2 store2_;

  //counted version
  map_first_level arg_hashmap_;
  map_second_level name_hashmap_;

  std::mutex mtx_;
};