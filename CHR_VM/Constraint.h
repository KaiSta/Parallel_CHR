#pragma once

#include <tbb/concurrent_vector.h>
#include <stdarg.h>
#include <functional>
#include <string>
#include <random>
// uncomment to disable assert()
// #define NDEBUG
#include <cassert>
#include <atomic>
#include <sstream>
#include <vector>

#include "placeholder.h"
#include "MurmurHash3.h"
#include "Rule_Types.h"

#include "ID_Manager.h"

enum constraint_state
{
  PENDING,
  CLAIMED,
  CONSUMED
};

template <typename value_type>
class Constraint
{
  friend class WorkerCHR;
  friend class Store;
  friend struct MatchChecklist;
public:
  Constraint(value_type one) : state_(PENDING), id_(ID_Manager::get())
  {
    args_.push_back(one);
  }
  Constraint(value_type one, value_type two) : state_(PENDING), id_(ID_Manager::get())
  {
    args_.push_back(one);
    args_.push_back(two);
  }

  Constraint(value_type one, value_type two, value_type three) : state_(PENDING), id_(ID_Manager::get())
  {
    args_.push_back(one);
    args_.push_back(two);
    args_.push_back(three);
  }

  Constraint(value_type one, value_type two, value_type three, value_type four) : state_(PENDING), id_(ID_Manager::get())
  {
    args_.push_back(one);
    args_.push_back(two);
    args_.push_back(three);
    args_.push_back(four);
  }

  Constraint(const std::vector<value_type>& args) : state_(PENDING), id_(ID_Manager::get())
  {
    for (auto& e : args)
    {
      args_.push_back(e);
    }
  }

  bool operator==(const Constraint<value_type>& other)
  {
    if (name_ != other.name_)
      return false;
    if (args_.size() != other.args_.size())
      return false;

    for (size_t i = 0; i < args_.size(); ++i)
    {
      if (args_[i] != other.args_[i])
        return false;
    }
    return true;
  }

  bool is_same(uint32_t name, const std::vector<uint64_t>& other_args)
  {
    if (name_ != name)
      return false;

    if (other_args.size() != args_.size())
      return false;

    for (size_t i = 0; i < args_.size(); ++i)
    {
      if (args_[i] != other_args[i])
        return false;
    }
    return true;
  }

  void set_template(Constraint<placeholder>* t)
  {
    template_ = t;
  }

  Constraint<placeholder>* get_template()
  {
    return template_;
  }

  void set_name_(uint32_t name)
  {
    name_ = name;
  }

  uint32_t get_name()
  {
    return name_;
  }

  constraint_state get_state()
  {
    return state_;
  }

  void set_state(constraint_state s)
  {
    state_.store(s);
  }

  uint32_t get_id()
  {
    return id_;
  }

  uint64_t hash()
  {
    return murmurhash3_64bit(args_[0]);
  }

  value_type get(size_t idx) const
  {
    assert(idx < args_.size());
    return args_[idx];
  }

  std::string str() const
  {
    std::stringstream ss;
    ss << "(";
    for (size_t i = 0; i < args_.size(); ++i)
    {
      ss << args_[i];

      if ((i + 1) < args_.size())
        ss << ",";
    }
    ss << ")";
    return ss.str();
  }

private:
  uint32_t name_;
  uint32_t id_;
  std::vector<value_type> args_;
  std::atomic<constraint_state> state_;

  Constraint<placeholder>* template_;
};

template <>
class Constraint<placeholder>
{
  friend struct MatchChecklist;

public:
  Constraint(placeholder lhs) : name_(get_id())
  {
    placeholders_.push_back(lhs);
  }

  Constraint(placeholder lhs, placeholder rhs) : name_(get_id())
  {
    placeholders_.push_back(lhs);
    placeholders_.push_back(rhs);
  }

  Constraint(placeholder p1, placeholder p2, placeholder p3) : name_(get_id())
  {
    placeholders_.push_back(p1);
    placeholders_.push_back(p2);
    placeholders_.push_back(p3);
  }

  Constraint(placeholder p1, placeholder p2, placeholder p3, placeholder p4) : name_(get_id())
  {
    placeholders_.push_back(p1);
    placeholders_.push_back(p2);
    placeholders_.push_back(p3);
    placeholders_.push_back(p4);
  }
  Constraint()
  {}

  Constraint<uint64_t>* get_instance(uint64_t n)
  {
    assert(placeholders_.size() == 1);
    Constraint<uint64_t>* c = new Constraint<uint64_t>(n);
    c->set_name_(name_);
    c->set_template(this);
    return c;
  }

  Constraint<uint64_t>* get_instance(uint64_t n, uint64_t m)
  {
    assert(placeholders_.size() == 2);
    Constraint<uint64_t>* c = new Constraint<uint64_t>(n,m);
    c->set_name_(name_);
    c->set_template(this);
    return c;
  }

  Constraint<uint64_t>* get_instance(uint64_t n, uint64_t m, uint64_t o)
  {
    assert(placeholders_.size() == 3);
    Constraint<uint64_t>* c = new Constraint<uint64_t>(n, m, o);
    c->set_name_(name_);
    c->set_template(this);
    return c;
  }

  Constraint<uint64_t>* get_instance(uint64_t n, uint64_t m, uint64_t o, uint64_t p)
  {
    assert(placeholders_.size() == 4);
    Constraint<uint64_t>* c = new Constraint<uint64_t>(n, m, o, p);
    c->set_name_(name_);
    c->set_template(this);
    return c;
  }

  Constraint<uint64_t>* get_instance(std::vector<uint64_t> args)
  {
    Constraint<uint64_t>* c = new Constraint<uint64_t>(args);
    c->set_name_(name_);
    c->set_template(this);
    return c;
  }

  Constraint<placeholder> sibling(std::vector<placeholder> placeholders)
  {
    Constraint<placeholder> sib;
    sib.name_ = name_;
    sib.placeholders_ = placeholders;
    return sib;
  }

  bool operator==(const Constraint<placeholder>& c)
  {
    return c.get_name() == name_;
  }
  bool operator==(Constraint<placeholder>* c)
  {
    return c->get_name() == name_;
  }

  uint32_t get_name() const
  {
    return name_;
  }

  static const size_t max_arg_count = 4;

private:
  std::vector<placeholder> placeholders_;

  uint32_t name_;

  static uint32_t get_id()
  {
    static std::atomic<uint32_t> id{ 0 };
    return id++;
  }

  std::string get_id(size_t len)
  {
    static const std::vector< std::string > alphabet {{ "0123456789", "abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ" }};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> outer(0, 2);
    std::uniform_int_distribution<> inner_numbers(0, 9);
    std::uniform_int_distribution<> inner_alphabet(0, 25);

    std::string res("");

    for (size_t i = 0; i < len; ++i)
    {
      uint32_t outer_index = outer(gen);
      uint32_t inner_index = (outer_index > 0) ? inner_alphabet(gen) : inner_numbers(gen);
      res += alphabet[outer_index][inner_index];
    }
    return res;
  }
};
