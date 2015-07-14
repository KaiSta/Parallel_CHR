#include "WorkerCHR.h"
#include <unordered_set>
#include "Match_Checklist.h"
#include "MurmurHash3.h"
#include <thread>
#include <tbb/concurrent_queue.h>

#include <future>
#include <list>
#include <stack>
#include <mutex>
#include <tuple>
#include <fstream>
#include "Stats.h"

WorkerCHR::WorkerCHR(Store* s, bool use_mpi) : store_(s), gen(rd()), abort_{ false }, async_count_{ 0 }, pool_(std::thread::hardware_concurrency()), use_mpi_(use_mpi), used_mode_(NONE)
{
}


WorkerCHR::~WorkerCHR()
{
}

void WorkerCHR::add_rule(Rule* r)
{
  rules_.push_back(r);
}

/*
An active constraint is code which is evaluated like a procedure call. If, at
the moment, no rule is applicable that removes it, the active constraint becomes
passive data in the constraint store. It is called (currently) passive (delayed,
suspended, sleeping, waiting).
*/

// old, without store proxy optimization, see run_parallel2
void WorkerCHR::run_parallel(size_t limit, mode m)
{
  size_t failed = 0; //unused

  std::vector<std::future< std::tuple<Constraint<uint64_t>*, bool, bool> > > handles;
  while (!queue_.empty())
  {
    for (size_t i = 0; !queue_.empty() && i < limit; ++i)
    {
      Constraint<uint64_t>* goal = nullptr;
      if (queue_.try_pop(goal))
      {
        if (goal->state_ == PENDING)
        {
          switch (m) {
          case PESSIMISTIC_RESTART:
            handles.push_back(std::async(std::launch::async, &WorkerCHR::try_goal_sj, this, goal, i, limit));
            break;
          case OPTIMISTIC_RESTART:
            handles.push_back(std::async(std::launch::async, &WorkerCHR::try_goal_sj_opt, this, goal, i, limit));
            break;
          default:
            handles.push_back(std::async(std::launch::async, &WorkerCHR::try_goal_sj_opt_partial_restart, this, goal, i, limit));
          }
        }
        else if (goal->state_ == CLAIMED)
          queue_.push(goal);
      }
    }

    for (auto& h : handles)
    {
      auto res = h.get();

      if (std::get<1>(res) && std::get<0>(res)->state_ == CONSUMED)//triggered a rule
      {
        failed = 0;
      }
      else if (std::get<2>(res)) // retry because the needed constraint was only claimed not consumed or simpagation rule
      {
        //try_goal_sj failed but saw the needed constraint as claimed -> retry
        queue_.push(std::get<0>(res));
        failed = 0;
      }
      else //failed
      {
        //constraint didn't trigger any rule, and becomes passive data
        ++failed;
      }
    }

    handles.clear();
    handles.reserve(limit);
  }
}

void WorkerCHR::add_constraint(Constraint<uint64_t>* c)
{
  store_->insert(c);
  queue_.push(c);
}

// old, without store proxy optimization, see try_goal_sj2
std::tuple<Constraint<uint64_t>*, bool, bool> WorkerCHR::try_goal_sj(Constraint<uint64_t>* c, size_t thread_num, size_t num_threads)
{
  constraint_state expected = PENDING;
  if (!c->state_.compare_exchange_weak(expected, CLAIMED))
  {
    if (c->state_ == CLAIMED || c->state_ == PENDING)
      return std::make_tuple(c, false, true); //other thread stole it but didn't consume it
    else
      return std::make_tuple(c, false, false); //other thread stole it and consumed it
  }
    
  bool saw_claimed = false;

  for (auto& rule : rules_)
  {
    MatchChecklist checklist(rule);
    if (!checklist.is_match(c)) // add goal to checklist
    {
      continue; //goal isn't part of this rule, switch to the next one
    }

    while (!checklist.is_complete())
    {
      auto h = checklist.get_hint(); //returns a hint for the most restricted needed constraint (name, arg restrictions)
      Store::store_map::const_accessor a;

      bool found_constraint = false;

      if (h.args) //with argument restrictions
      {
        Store::store_map2::const_accessor b;
        store_->store2_.find(b, h.name);

        uint64_t key = 0;
        for (uint64_t i = 0; i < h.arg_hints_.size(); ++i)
        {
          if (h.arg_hints_[i].first)
            key ^= murmurhash3_64bit(h.arg_hints_[i].second);
        }

        if (!b.empty())
          b->second.find(a, key);
      }
      else //without argument restrictions
      {
        store_->store_.find(a, h.name);
      }

      if (!a.empty())
      {

        //WITH RANDOM ACCESS
        //std::uniform_int_distribution<> range(0, (a->second.size() - 1));
        //size_t offset = (a->second.size() / num_threads) * thread_num; //range(gen);

        //for (size_t i = 0; i < a->second.size(); ++i)
        //{
        //  size_t index = (offset + i) % a->second.size();

        //  auto e = a->second[index];

        //  if (e != c && e->state_ != CONSUMED && checklist.convenient(e))
        //  {
        //    constraint_state state_partner = PENDING;
        //    if (e->state_.compare_exchange_weak(state_partner, CLAIMED))
        //    {
        //      if (checklist.add(e))
        //      {
        //        found_constraint = true;
        //        break; //match for given hint found
        //      }
        //    }
        //    else
        //    {
        //      if (e->state_ == CLAIMED)
        //        saw_claimed = true;
        //    }
        //  }
        //}

        //WITHOUT RANDOM ACCESS
        for (auto& e : a->second)
        {
          if (e != c && e->state_ != CONSUMED && checklist.convenient(e))
          {
            constraint_state state_partner = PENDING;
            if (e->state_.compare_exchange_weak(state_partner, CLAIMED))
            {
              if (checklist.add(e))
              {
                found_constraint = true;
                break; //match for given hint found
              }
            }
            else
            {
              if (e->state_ == CLAIMED)
                saw_claimed = true;
            }
          }
        }
      }

      if (!found_constraint)
      {
        for (auto& e : checklist.used_constraints_) // set all constraints back to PENDING
        {
          constraint_state expected = CLAIMED;
          if (!e->state_.compare_exchange_weak(expected, PENDING))
            printf("error while returning constraint\n"); //should never happen
        }
        break; //didn't find a constraint for given hint
      }
    }

    if (checklist.is_complete())
    {
      switch (rule->type_)
      {
      case SIMPLIFICATION: {
        for (auto& e : checklist.used_constraints_)
        {
          constraint_state expected = CLAIMED;
          if (!(e->state_.compare_exchange_weak(expected, CONSUMED)))
          {
            printf("expected CLAIMED to set CONSUMED, was %i\n", expected);//should never happen
          }
        }
        //rule->body_({ checklist.used_constraints_.begin(), checklist.used_constraints_.end() });
        rule->body_(checklist.get_rule_ordered());
      }
        break;
      
      case SIMPAGATION: {
        for (auto& e : checklist.consumable_)
        {
          constraint_state expected = CLAIMED;
          if (!(e->state_.compare_exchange_weak(expected, CONSUMED)))
          {
            printf("expected CLAIMED to set CONSUMED, was %i\n", expected);//should never happen
          }
        }
        for (auto& e : checklist.used_constraints_)
        {
          if (e->state_ != CONSUMED)
          {
            constraint_state expected = CLAIMED;
            e->state_.compare_exchange_weak(expected, PENDING);
          }
        }
        //rule->body_({ checklist.used_constraints_.begin(), checklist.used_constraints_.end() });
        rule->body_(checklist.get_rule_ordered());
      }
        break;
      }
      return std::make_tuple(c, true, false); //rules loop
    }
  }

  return std::make_tuple(c, false, saw_claimed);
}

// old, without store proxy optimization, see try_goal_sj_opt2
std::tuple<Constraint<uint64_t>*, bool, bool> WorkerCHR::try_goal_sj_opt(Constraint<uint64_t>* c, size_t thread_num, size_t num_threads)
{
  for (auto& rule : rules_)
  {
    MatchChecklist checklist(rule);
    if (!checklist.is_match(c))
    {
      continue;
    }

    while (!checklist.is_complete())
    {
      auto h = checklist.get_hint();
      Store::store_map::const_accessor a;

      bool found_constraint = false;

      if (h.args)
      {
        Store::store_map2::const_accessor b;
        store_->store2_.find(b, h.name);
        
        uint64_t key = 0;
        for (uint64_t i = 0; i < h.arg_hints_.size(); ++i)
        {
          if (h.arg_hints_[i].first)
            key ^= murmurhash3_64bit(h.arg_hints_[i].second);
        }
        if (!b.empty())
          b->second.find(a, key);
      }
      else
      {
        store_->store_.find(a, h.name);
      }
      
      if (!a.empty())
      {
        std::uniform_int_distribution<> range(0, (static_cast<int>(a->second.size()) - 1));
        size_t offset = range(gen); //(a->second.size() / num_threads) * thread_num;

        for (size_t i = 0; i < a->second.size(); ++i)
        {
          size_t index = (offset + i) % a->second.size();

          if (a->second[index]->state_ != CONSUMED &&  a->second[index] != c)
          {
            if (checklist.convenient(a->second[index]))
            {
              checklist.add(a->second[index]);
              found_constraint = true;
              break;
            }
          }
        }

        /*for (auto& e : a->second)
        {
          if (e->state_ != CONSUMED &&  e != c)
          {
            if (checklist.convenient(e))
            {
              checklist.add(e);
              found_constraint = true;
              break;
            }
          }
        }*/
      }

      if (!found_constraint)
      {
        break;
      }
    }

    if (checklist.is_complete())
    {
      bool consumed_all = true;

      std::vector<Constraint<uint64_t>*> claimed_constraints;
      
      for (auto& e : checklist.used_constraints_)
      {
        constraint_state expected = PENDING;
        if (!(e->state_.compare_exchange_weak(expected, CLAIMED)))
        {
          consumed_all = false;
          break;
        }
        else
          claimed_constraints.push_back(e);
      }
    

      if (!consumed_all)
      {
        for (auto& e : claimed_constraints)
        {
          constraint_state expected = CLAIMED;
          e->state_.compare_exchange_weak(expected, PENDING);
        }
      }

      switch (rule->type_)
      {
        case SIMPLIFICATION: {
          if (consumed_all)
          {
            for (auto& e : claimed_constraints)
            {
              constraint_state expected = CLAIMED;
              e->state_.compare_exchange_weak(expected, CONSUMED);
            }
            //rule->body_({ checklist.used_constraints_.begin(), checklist.used_constraints_.end() });
            rule->body_(checklist.get_rule_ordered());
          }
        }
          break;

        case SIMPAGATION: {

          if (consumed_all)
          {
            for (auto& e : checklist.consumable_)
            {
              constraint_state expected = CLAIMED;
              e->state_.compare_exchange_weak(expected, CONSUMED);
            }
            for (auto& e : claimed_constraints)
            {
              if (e->state_ != CONSUMED)
              {
                constraint_state expected = CLAIMED;
                e->state_.compare_exchange_weak(expected, PENDING);
              }
            }
           // rule->body_({ checklist.used_constraints_.begin(), checklist.used_constraints_.end() });
            rule->body_(checklist.get_rule_ordered());
          }

        }
          break;
      }
      return std::make_tuple(c, consumed_all, true); //rules loop
    }
  }
  return std::make_tuple(c, false, false);
}

// old, without store proxy optimization, see try_matching2
void WorkerCHR::try_matching(Constraint<uint64_t>* c, MatchChecklist& checklist, size_t thread_num, size_t num_threads)
{
  while (!checklist.is_complete())
  {
    auto h = checklist.get_hint();
    Store::store_map::const_accessor a;

    bool found_constraint = false;

    if (h.args)
    {
      Store::store_map2::const_accessor b;
      store_->store2_.find(b, h.name);

      uint64_t key = 0;
      for (uint64_t i = 0; i < h.arg_hints_.size(); ++i)
      {
        if (h.arg_hints_[i].first)
          key ^= murmurhash3_64bit(h.arg_hints_[i].second);
      }
      if (!b.empty())
        b->second.find(a, key);
    }
    else
    {
      store_->store_.find(a, h.name);
    }

    if (!a.empty())
    {
      std::uniform_int_distribution<> range(0, (static_cast<int>(a->second.size()) - 1));
      size_t offset = range(gen);
      //size_t offset = (a->second.size() / num_threads) * thread_num; //range(gen);

      for (size_t i = 0; i < a->second.size(); ++i)
      {
        size_t index = (offset + i) % a->second.size();

        if (a->second[index]->state_ != CONSUMED &&  a->second[index] != c)
        {
          if (checklist.convenient(a->second[index]))
          {
            checklist.add(a->second[index]);
            found_constraint = true;
            break;
          }
        }
      }

      /*for (auto& e : a->second)
      {
        if (e->state_ != CONSUMED && e != c)
        {
          if (checklist.convenient(e))
          {
            checklist.add(e);
            found_constraint = true;
            break;
          }

        }
      }*/
    }

    if (!found_constraint)
    {
      break;
    }
  }
}
// old, without store proxy optimization, see try_goal_sj_opt_partial_restart2
std::tuple<Constraint<uint64_t>*, bool, bool> WorkerCHR::try_goal_sj_opt_partial_restart(Constraint<uint64_t>* c, size_t thread_num, size_t num_threads)
{
  Rule* r = nullptr;
  std::unordered_set<Constraint<uint64_t>*> found;

  for (auto& rule : rules_)
  {
    MatchChecklist list(rule);
    if (list.is_match(c))
    {
      r = rule;
      break;
    }
  }
  MatchChecklist checklist(r);
  checklist.add(c);
  bool abort = false;

  for (size_t i = 0; i < 3 && c->state_ != CONSUMED && !abort; ++i)
  {
    try_matching(c, checklist, thread_num, num_threads);

    if (!checklist.is_complete())
    { //anstatt gleich abzubrechen rule switch einfuehren. erst wenn alle regeln fehlschlagen return
      return std::make_tuple(c, false, false);
    }
    else
    {
      bool claimed_all = true;
      std::vector<Constraint<uint64_t>* > claimed;
      
      //try to claim them
      for (auto& e : checklist.used_constraints_)
      {
        constraint_state expected = PENDING;
        if (!(e->state_.compare_exchange_weak(expected, CLAIMED)))
        {
          claimed_all = false;
          checklist.remove_constraint(e);

          if (e == c)
            abort = true;

          break;
        }
        else
        {
          claimed.push_back(e);
        }
      }

      if (!claimed_all || abort)
      {
        for (auto& e : claimed)
        {
          constraint_state expected = CLAIMED;
          e->state_.compare_exchange_weak(expected, PENDING);
        }
      }
      
      if (claimed_all && !abort)
      {
        switch (r->type_)
        {
        case SIMPLIFICATION:
        {
          for (auto& e : claimed)
          {
            constraint_state expected = CLAIMED;
            e->state_.compare_exchange_weak(expected, CONSUMED);
          }
          //r->body_({ checklist.used_constraints_.begin(), checklist.used_constraints_.end() });
          r->body_(checklist.get_rule_ordered());
        }
        break;

        case SIMPAGATION: 
        {
          for (auto& e : checklist.consumable_)
          {
            constraint_state expected = CLAIMED;
            e->state_.compare_exchange_weak(expected, CONSUMED);
          }
          for (auto& e : claimed)
          {
            if (e->state_ != CONSUMED)
            {
              constraint_state expected = CLAIMED;
              e->state_.compare_exchange_weak(expected, PENDING);
            }
          }

          //r->body_({ checklist.used_constraints_.begin(), checklist.used_constraints_.end() });
          r->body_(checklist.get_rule_ordered());
        }
                  break;
        }
        return std::make_tuple(c, true, false);
      } // if success at claiming
    } // checklist completed
  } //retries
  //printf("timout\n");
  return std::make_tuple(c, false, true);
}

std::vector<Constraint<uint64_t>*> WorkerCHR::get_result()
{
  std::vector<Constraint<uint64_t>*> res;
  for (auto& e : store_->store_)
  {
    for (auto& t : e.second)
    {
      if (t->state_ != CONSUMED)
        res.push_back(t);
    }
  }
  return res;
}

std::vector<Constraint<uint64_t>*> WorkerCHR::get_result2()
{
  std::vector<Constraint<uint64_t>*> res;
  for (auto& e : store_->name_hashmap_)
  {
    for (auto& t : e.second)
    {
      if (t->num_pendings() > 0)
        res.push_back(t->constraint);
    }
  }
  return res;
}

Store::status* WorkerCHR::add_constraint(Constraint<placeholder>& c, std::vector<uint64_t> args, Store::modi m)
{
  Store::status* tmp = store_->insert_counted(c, args, m);
  stati_queue_.push(tmp);
  return tmp;
}


void WorkerCHR::run_parallel2(size_t limit, mode m)
{
  size_t failed = 0;
  std::vector<std::future< std::tuple<Store::status*, bool, bool> > > handles;
  while (!stati_queue_.empty())
  {
    for (size_t i = 0; !stati_queue_.empty() && i < limit; ++i)
    {
      Store::status* goal = nullptr;
      if (stati_queue_.try_pop(goal))
      {
        if (goal->num_pendings() > 0)
        {
          switch (m) {
          case PESSIMISTIC_RESTART:
            handles.push_back(std::async(std::launch::async, &WorkerCHR::try_goal_sj2, this, goal, i, limit));
            break;
          case OPTIMISTIC_RESTART:
            handles.push_back(std::async(std::launch::async, &WorkerCHR::try_goal_sj_opt2, this, goal, i, limit));
            break;
          case OPTIMISTIC_PARTIAL_RESTART:
            handles.push_back(std::async(std::launch::async, &WorkerCHR::try_goal_sj_opt_partial_restart2, this, goal, i, limit));
            break;
          }

        }
        else if (goal->num_claims() > 0)
          stati_queue_.push(goal);
      }
    }

    for (auto& h : handles)
    {
      auto res = h.get();

      //if (std::get<1>(res))//triggered a rule
      //{
      //  failed = 0;
      //}
      //else if (std::get<2>(res)) //didn't trigger but retry because the needed constraint was only claimed not consumed
      //{
      //  //try_goal_sj failed but saw the needed constraint as claimed -> retry
      //  stati_queue_.push(std::get<0>(res));
      //  failed = 0;
      //}
      
      if (std::get<2>(res)) // retry because the needed constraint was only claimed not consumed or part of the keeps for simpagation
      {
        //try_goal_sj failed but saw the needed constraint as claimed -> retry
        stati_queue_.push(std::get<0>(res));
        failed = 0;
      }
      else if (std::get<1>(res))//triggered a rule
      {
        failed = 0;
      }
      else //failed
      {
        //constraint didn't trigger any rule, and becomes passive data
        ++failed;
      }
    }

    handles.clear();
    handles.reserve(limit);
  }
}


std::tuple<Store::status*, bool, bool> WorkerCHR::try_goal_sj2(Store::status* c, size_t thread_num, size_t num_threads)
{
  bool retry = false;

  if (!c->try_claim(retry))
  {
    //if (retry)
    //  return std::make_tuple(c, false, true); //other thread stole it but didn't consume it
    //else
    //  return std::make_tuple(c, false, false); //other thread stole it and consumed it
    //if (retry || c->num_claims() > 0 || c->num_pendings() > 0)
      return std::make_tuple(c, false, ((c->pendings_and_claims() > 0) || retry));
    //else
    //  return std::make_tuple(c, false, false);
  }
  
  bool saw_claimed = false;

  for (auto& rule : rules_)
  {

    MatchChecklist checklist(rule);
    if (!checklist.is_match(c))
    {
      continue;
    }

    std::stack<std::tuple<uint64_t, uint64_t, Store::status*> > last_constraint;
    bool use_filter = false;

    while (!checklist.is_complete())
    {
      /*Store::status* filter = nullptr;
      if (!last_constraint_.empty() && use_filter)
      {
        filter = last_constraint_.top();
        last_constraint_.pop();
        use_filter = false;
      }*/
      uint64_t start_index = 0;
      uint64_t last_index = std::numeric_limits<uint64_t>::max();
      Store::status* filter = nullptr;
      if (!last_constraint.empty() && use_filter)
      {
        filter      = std::get<2>(last_constraint.top());
        start_index = std::get<0>(last_constraint.top());
        last_index  = std::get<1>(last_constraint.top());
        last_constraint.pop();
        use_filter = false;
      }

      auto h = checklist.get_hint();
      bool found_constraint = false;
      Store::map_second_level::const_accessor b;

      if (h.args)
      {
        Store::map_first_level::const_accessor a;

        store_->arg_hashmap_.find(a, h.name);

        uint64_t key = 0;
        for (uint64_t i = 0; i < h.arg_hints_.size(); ++i)
        {
          if (h.arg_hints_[i].first)
            key += murmurhash3_64bit(h.arg_hints_[i].second);
        }

        if (!a.empty())
          a->second.find(b, key);
      }
      else
      {
        store_->name_hashmap_.find(b, h.name);
      }

      if (!b.empty())
      {
        last_index = std::min(last_index, b->second.size());

        for(uint64_t i = (start_index); i < last_index; ++i)
        {
          auto e = b->second[i];

          if (((e->num_pendings() > 0) || (e->num_claims() > 0)) && e != filter &&
            checklist.convenient(e))
          {
            bool ret = false;
            if (e->try_claim(ret))
            {
              found_constraint = true;
              checklist.add(e);
              last_constraint.push(std::make_tuple(i, b->second.size(), e));
              break;
            }
            else
            {
              if (e->constraint->id_ == c->constraint->id_)
              {
                if (e->num_claims() > 1 || e->num_pendings() > 0)
                  saw_claimed = true;
              }
              else
                if (e->num_claims() > 0 || e->num_pendings() > 0)
                  saw_claimed = true;
            }
          }
        }
      }


      if (!found_constraint)
      {
        if (last_constraint.empty())
        {
          for (auto& e : checklist.used_stati_) // set all constraints back to PENDING
          {
            e->rollback();
          }
          break; //didn't find a constraint for given hint
        }
        else
        {
          use_filter = true;
          std::get<2>(last_constraint.top())->rollback();
          checklist.remove_constraint(std::get<2>(last_constraint.top()));
        }
      }
    }

    if (checklist.is_complete())
    {
      bool jumped_myself = false;
      for (auto& e : checklist.used_stati_)
      {
        e->consume();
        if (e != c || jumped_myself)
        {
          e->consumed_tokens.push(1);
        }
        else
          jumped_myself = true;
      }

      rule->body_(checklist.get_rule_ordered());

      if (rule->type_ == SIMPAGATION)
      {
        if (use_mpi_)
        {
          for (auto& e : checklist.keep_stati_)
          {
            if (e->m == Store::modi::SYNC)
              sync((*(*e).constraint->get_template()), (*e).constraint->args_, used_mode_);
            else
              async((*(*e).constraint->get_template()), (*e).constraint->args_, used_mode_);
          }
        }
        else
        {
          for (auto& e : checklist.keep_stati_)
          {
            add_constraint((*(*e).constraint->get_template()), (*e).constraint->args_, e->m);
          }
        }
      }

      
      return std::make_tuple(c, true, false);
    }  
  }
  return std::make_tuple(c, false, saw_claimed);
}

std::tuple<Store::status*, bool, bool> WorkerCHR::try_goal_sj_opt2(Store::status* c, size_t thread_num, size_t num_threads)
{
  bool saw_claimed = false;
  bool do_retry = false;

  for (auto& rule : rules_)
  {
    MatchChecklist checklist(rule);
    if (!checklist.is_match(c))
    {
      continue;
    }

    std::stack<std::tuple<uint64_t, uint64_t, Store::status*> > last_constraint;
    bool use_filter = false;

    while (!checklist.is_complete())
    {
      /*Store::status* filter = nullptr;
      if (!last_constraint_.empty() && use_filter)
      {
        filter = last_constraint_.top();
        last_constraint_.pop();
        use_filter = false;
      }*/
      uint64_t start_index = 0;
      uint64_t last_index = std::numeric_limits<uint64_t>::max();
      Store::status* filter = nullptr;
      if (!last_constraint.empty() && use_filter)
      {
        start_index = std::get<0>(last_constraint.top());
        last_index  = std::get<1>(last_constraint.top());
        filter      = std::get<2>(last_constraint.top());
        last_constraint.pop();
        use_filter = false;
      }

      auto h = checklist.get_hint();
      Store::map_second_level::const_accessor a;

      bool found_constraint = false;

      if (h.args)
      {
        Store::map_first_level::const_accessor b;

        store_->arg_hashmap_.find(b, h.name);

        uint64_t key = 0;
        for (uint64_t i = 0; i < h.arg_hints_.size(); ++i)
        {
          if (h.arg_hints_[i].first)
            key += murmurhash3_64bit(h.arg_hints_[i].second);// ^ i;
        }

        if (!b.empty())
          b->second.find(a, key);
      }
      else
      {
        store_->name_hashmap_.find(a, h.name);
      }

      if (!a.empty())
      {
        //std::uniform_int_distribution<> range(0, (static_cast<int>(a->second.size()) - 1));
        //size_t offset =  range(gen); //(a->second.size() / num_threads) * thread_num;

        //for (size_t i = 0; i < a->second.size(); ++i)
        //{
        //  size_t index = (offset + i) % a->second.size();
        //  auto e = a->second[index];

        //  if (e->num_claims() > 0)
        //    saw_claimed = true;

        //  if (((e->constraint->id_ != c->constraint->id_ &&
        //    ((e->num_pendings() > 0) || (e->num_claims() > 0))) ||
        //    e->constraint->id_ == c->constraint->id_ &&
        //    ((e->num_pendings() > 1) || (e->num_claims() > 1))) && e != filter)
        //  {
        //    if (checklist.convenient(e))
        //    {
        //      checklist.add(e);
        //      found_constraint = true;
        //      last_constraint_.push(e);
        //      //if ((i + 1) < a->second.size()) // at least one alternative
        //      //{
        //      //  last_constraint_.push(e);
        //      //}
        //      break;
        //    }
        //  }
        //}

       // if (!filter)
        //  last_index = a->second.size();
        last_index = std::min(last_index, a->second.size());
        
        if (last_index > a->second.size())
          printf("lastindex > size\nl=%i, s=%i", last_index, a->second.size());

        for(uint64_t i = (start_index); i < last_index/*a->second.size()*/; ++i)//for (auto& e : a->second)
        {
          auto e = a->second[i];
          if (((e->constraint->id_ != c->constraint->id_ && 
            ((e->num_pendings() > 0) || (e->num_claims() > 0))) ||
            e->constraint->id_ == c->constraint->id_ && 
            ((e->num_pendings() > 1) || (e->num_claims() > 1))) && e != filter)
          {
            if (checklist.convenient(e))
            {
              checklist.add(e);
              found_constraint = true;
              last_constraint.push(std::make_tuple(i, a->second.size(), e));

              break;
            }
          }
        }
      }

      if (!found_constraint)
      {
        if (last_constraint.empty())
        {
          break; //didn't find a constraint for given hint
        }
        else
        {
          use_filter = true;
          checklist.remove_constraint(std::get<2>(last_constraint.top()));
        }
      }
    }

    if (checklist.is_complete())
    {
      do_retry = true;
      bool consumed_all = true;
      std::vector<Store::status*> claimed_stats;

      for (auto& e : checklist.used_stati_)
      {
        bool retry = false;
        if (e->try_claim(retry))
        {
          claimed_stats.push_back(e);
        }
        else
        {
          consumed_all = false;
          //hier muss noch ein zusaetzliches update von "saw claimed" hin, da die commit stage failen kann weil nachtraeglich was geclaimed wurd (bei pess ja nicht der fall)
          if (e->num_claims() > 0)
            saw_claimed = true;

          break;
        }
      }


      if (!consumed_all)
      {
        for (auto& e : claimed_stats)
        {
          e->rollback();
        }
      }
      else
      {
        bool jumped_myself = false;
        for (auto& e : claimed_stats)
        {
          e->consume();
          if (e != c || jumped_myself)
          {
            e->consumed_tokens.push(1);
          }
          else
            jumped_myself = true;
        }
        
        rule->body_(checklist.get_rule_ordered());

        if (rule->type_ == SIMPAGATION)
        {
          if (use_mpi_)
          {
            for (auto& e : checklist.keep_stati_)
            {
              if (e->m == Store::modi::SYNC)
                sync((*(*e).constraint->get_template()), (*e).constraint->args_, used_mode_);
              else
                async((*(*e).constraint->get_template()), (*e).constraint->args_, used_mode_);
            }
          }
          else
          {
            for (auto& e : checklist.keep_stati_)
            {
              add_constraint((*(*e).constraint->get_template()), (*e).constraint->args_, e->m);
            }
          }
        }        
      }
      return std::make_tuple(c, consumed_all, !consumed_all);
    }
  }
  return std::make_tuple(c, false, do_retry); // always tells him to do some retry, so never terminates
}

bool WorkerCHR::try_matching2(Store::status* c, MatchChecklist& checklist, size_t thread_num, size_t num_threads)
{
  bool ret = false;

  std::stack<std::tuple<uint64_t, uint64_t, Store::status*> > last_constraint;
  bool use_filter = false;

  while (!checklist.is_complete())
  {
    //Store::status* filter = nullptr;
    //uint64_t start_index = 0;
    //
    //if (!last_constraint.empty() && use_filter)
    //{
    //  //filter = last_constraint_.top();
    //  start_index = last_constraint.top().first;
    //  filter = last_constraint.top().second;
    //  last_constraint.pop();
    //  use_filter = false;
    //}

    uint64_t start_index = 0;
    uint64_t last_index = std::numeric_limits<uint64_t>::max();
    Store::status* filter = nullptr;
    if (!last_constraint.empty() && use_filter)
    {
      start_index = std::get<0>(last_constraint.top());
      last_index = std::get<1>(last_constraint.top());
      filter = std::get<2>(last_constraint.top());
      last_constraint.pop();
      use_filter = false;
    }

    auto h = checklist.get_hint();
    Store::map_second_level::const_accessor a;

    bool found_constraint = false;

    if (h.args)
    {
      Store::map_first_level::const_accessor b;
      store_->arg_hashmap_.find(b, h.name);

      uint64_t key = 0;
      for (uint64_t i = 0; i < h.arg_hints_.size(); ++i)
      {
        if (h.arg_hints_[i].first)
          key += murmurhash3_64bit(h.arg_hints_[i].second);
      }
      if (!b.empty())
        b->second.find(a, key);
    }
    else
    {
      store_->name_hashmap_.find(a, h.name);
    }

    if (!a.empty())
    {
      /*std::uniform_int_distribution<> range(0, (static_cast<int>(a->second.size()) - 1));
      size_t offset = range(gen);*/
      //size_t offset = (a->second.size() / num_threads) * thread_num; //range(gen);

      //for (size_t i = 0; i < a->second.size(); ++i)
      //{
      //  size_t index = (offset + i) % a->second.size();    

      //  if (a->second[index]->num_claims() > 0)
      //    ret = true;

      //  if (((a->second[index]->constraint->id_ != c->constraint->id_ &&
      //    ((a->second[index]->num_pendings() > 0) || (a->second[index]->num_claims() > 0))) ||
      //     a->second[index]->constraint->id_ == c->constraint->id_ &&
      //     ((a->second[index]->num_pendings() > 1) || (a->second[index]->num_claims() > 1))) && a->second[index] != filter)
      //  {
      //    if (checklist.convenient(a->second[index]))
      //    {
      //      checklist.add(a->second[index]);
      //      found_constraint = true;
      //      last_constraint_.push(a->second[index]);
      //      //if ((i + 1) < a->second.size()) // at least one alternative
      //      //{
      //      //  last_constraint_.push(a->second[index]);
      //      //}
      //      break;
      //    }
      //  }
      //}

      last_index = std::min(last_index, a->second.size());

      for (uint64_t i = (start_index); i < last_index; ++i)
      {
        auto e = a->second[i];
        if (((e->constraint->id_ != c->constraint->id_ &&
          ((e->num_pendings() > 0) || (e->num_claims() > 0))) ||
          e->constraint->id_ == c->constraint->id_ &&
          ((e->num_pendings() > 1) || (e->num_claims() > 1))) && e != filter)
        {
          if (checklist.convenient(e))
          {
            checklist.add(e);
            found_constraint = true;
            last_constraint.push(std::make_tuple( i, a->second.size(), e ));
            break;
          }
        }
      }

    }

    if (!found_constraint)
    {
      if (last_constraint.empty())
      {
        break; //didn't find a constraint for given hint
      }
      else
      {
        use_filter = true;
        checklist.remove_constraint(std::get<2>(last_constraint.top()));
      }
    }
  }
  return ret;
}

std::tuple<Store::status*, bool, bool> WorkerCHR::try_goal_sj_opt_partial_restart2(Store::status* c, size_t thread_num, size_t num_threads)
{
  static std::atomic<uint32_t> ctr{ 0 };
  Rule* r = nullptr;
  std::unordered_set<Constraint<uint64_t>*> found;

  for (auto& rule : rules_)
  {
    MatchChecklist list(rule);
    if (list.is_match(c))
    {
      r = rule;
      break;
    }
  }
  MatchChecklist checklist(r);
  checklist.add(c);
  bool abort = false;
  bool do_retry = true;

  for (size_t i = 0; i < 3 && ((c->num_pendings() > 0) || (c->num_claims() > 0)) && !abort; ++i)
  {
    auto ret = try_matching2(c, checklist, thread_num, num_threads);

    if (ret == false)
      do_retry = false;

    if (!checklist.is_complete())
    { //anstatt gleich abzubrechen rule switch einfuehren. erst wenn alle regeln fehlschlagen return
      return std::make_tuple(c, false, ret);
    }
    else
    {
      bool claimed_all = true;
      std::vector<Store::status*> claimed_stats;

      //try to claim them
      for (auto& e : checklist.used_stati_)
      {
        bool retry = false;
        if (e->try_claim(retry))
        {
          claimed_stats.push_back(e);
        }
        else
        {
          claimed_all = false;
          checklist.remove_constraint(e);

          if ((*e->constraint) == (*c->constraint))
            abort = true;
          break;
        }
      }

      if (!claimed_all || abort)
      {
        for (auto& e : claimed_stats)
        {
          e->rollback();
        }
      }
      else if (claimed_all && !abort)
      {
        bool jumped_myself = false;
        for (auto& e : claimed_stats)
        {
          e->consume();

          if ((e != c || jumped_myself) && e->m == Store::modi::SYNC)
          {
            e->consumed_tokens.push(1);
          }
          else
            jumped_myself = true;
        }
        r->body_(checklist.get_rule_ordered());

        if (r->type_ == SIMPAGATION)
        {
          if (use_mpi_)
          {
            for (auto& e : checklist.keep_stati_)
            {
              if (e->m == Store::modi::SYNC)
                sync((*(*e).constraint->get_template()), (*e).constraint->args_, used_mode_);
              else
                async((*(*e).constraint->get_template()), (*e).constraint->args_, used_mode_);
            }
          }
          else
          {
            for (auto& e : checklist.keep_stati_)
            {
              add_constraint((*(*e).constraint->get_template()), (*e).constraint->args_, e->m);
            }
          }
        }

        return std::make_tuple(c, true, false);
      }
    } // checklist completed
  } //retries
  return std::make_tuple(c, false, true);
}

void WorkerCHR::async(Constraint<placeholder>& c, std::vector<uint64_t> args, mode m)
{
  if (used_mode_ == NONE)
  {
    mode exp = NONE;
    used_mode_.compare_exchange_weak(exp, m);
  } 

  auto constraint = add_constraint(c, args);
  pool_.enqueue([=]() {

    std::tuple<Store::status*, bool, bool> res;
    backoff bkoff;

    while (constraint->pendings_and_claims() > 0)
    {
      switch (m)
      {
      case PESSIMISTIC_RESTART:
        res = try_goal_sj2(constraint, 0, 8);
        break;
      case OPTIMISTIC_RESTART:
        res = try_goal_sj_opt2(constraint, 0, 8);
        break;
      case OPTIMISTIC_PARTIAL_RESTART:
        res = try_goal_sj_opt_partial_restart2(constraint, 0, 8);
        break;
      }

      if (std::get<1>(res) || //rule triggered
        !std::get<2>(res)) // failed completly
        break;
      else
        bkoff();
    }
  });
}

void WorkerCHR::sync(Constraint<placeholder>& c, std::vector<uint64_t> args, mode m)
{
  if (used_mode_ == NONE)
  {
    mode exp;
    used_mode_.compare_exchange_weak(exp, m);
  }

  auto constraint = add_constraint(c, args, Store::modi::SYNC);
  bool succ = false;
  backoff bkoff;

  while (!succ)
  {
    std::tuple<Store::status*, bool, bool> res;
    
    switch (m)
    {
    case PESSIMISTIC_RESTART:
      res = try_goal_sj2(constraint, 0, 8);
      break;
    case OPTIMISTIC_RESTART:
      res = try_goal_sj_opt2(constraint, 0, 8);
      break;
    case OPTIMISTIC_PARTIAL_RESTART:
      res = try_goal_sj_opt_partial_restart2(constraint, 0, 8);
      break;
    }

    if (std::get<1>(res)) //rule triggered
    {
      succ = true;
    }
    else if (std::get<2>(res)) // retry
    {
      int token;
      if (constraint->consumed_tokens.try_pop(token))
        succ = true;
      else
        bkoff();
    }
    else
    {
      int token;
      constraint->consumed_tokens.pop(token);
      succ = true;
    }    
  }
}

void WorkerCHR::add_stati_(Store::status* s)
{
  stati_queue_.push(s);
}

std::vector<Constraint<uint64_t>*> WorkerCHR::get()
{
  pool_.join();

  std::vector<Constraint<uint64_t>*> res;
  for (auto& e : store_->name_hashmap_)
  {
    for (auto& j : e.second)
    {
      if (j->num_pendings() > 0)
        res.push_back(j->constraint);
    }
  }

  return res;
}

void WorkerCHR::wait()
{
  pool_.join();
}

void WorkerCHR::async1(Constraint<placeholder>& c, std::vector<uint64_t> args, mode m)
{
  std::atomic<uint64_t>* count = &async_count_;
  auto constraint = c.get_instance(args);
  add_constraint(constraint);

  pool_.enqueue([=]() {

    count->fetch_add(1);
    std::tuple<Constraint<uint64_t>*, bool, bool> res;
    backoff bkoff;

    while (constraint->get_state() != CONSUMED)
    {
      switch (m)
      {
      case PESSIMISTIC_RESTART:
        res = try_goal_sj(constraint, 0, 8);
        break;
      case OPTIMISTIC_RESTART:
        res = try_goal_sj_opt(constraint, 0, 8);
        break;
      case OPTIMISTIC_PARTIAL_RESTART:
        res = try_goal_sj_opt_partial_restart(constraint, 0, 8);
        break;
      }

      if (std::get<1>(res) || //rule triggered
        !std::get<2>(res)) // failed completly
        break;
      else
        bkoff();
    }

    if (constraint->get_state() == PENDING || constraint->get_state() == CLAIMED)
      add_c_(std::get<0>(res));

    count->fetch_sub(1);
  });
}

void WorkerCHR::sync1(Constraint<placeholder>& c, std::vector<uint64_t> args, mode m)
{
  auto constraint = c.get_instance(args);
  add_constraint(constraint);
  backoff bkoff;

  while (constraint->get_state() != CONSUMED && !abort_.load())
  {
    std::tuple<Constraint<uint64_t>*, bool, bool> res;

    switch (m)
    {
    case PESSIMISTIC_RESTART:
      res = try_goal_sj(constraint, 0, 8);
      break;
    case OPTIMISTIC_RESTART:
      res = try_goal_sj_opt(constraint, 0, 8);
      break;
    case OPTIMISTIC_PARTIAL_RESTART:
      res = try_goal_sj_opt_partial_restart(constraint, 0, 8);
      break;
    }

    if (std::get<1>(res)) //rule triggered
      break;
    else
      bkoff();
  }
}

std::vector<Constraint<uint64_t>*> WorkerCHR::get1()
{

  pool_.join();

  std::vector<Constraint<uint64_t>*> res;
  for (auto& e : store_->store_)
  {
    for (auto& j : e.second)
    {
      if (j->state_ == PENDING)
        res.push_back(j);
    }
  }

  return res;
}

void WorkerCHR::add_c_(Constraint<uint64_t>* c)
{
  queue_.push(c);
}

