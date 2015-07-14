#pragma once

#include <functional>
#include <unordered_map>
#include "Rule_Types.h"
#include "Constraint.h"

class Rule
{
  friend class WorkerCHR;
  friend struct MatchChecklist;

  typedef std::function<void(std::vector</*const*/ Constraint<uint64_t>* /*const*/>) > body_t;

public:

  Rule(std::initializer_list<Constraint<placeholder> > constraints, body_t body, 
    rule_type type = SIMPLIFICATION, bool ignore_placeholder_restrictions = false) : body_(body),
    type_(type), ignore_placeholder(ignore_placeholder_restrictions)
  {
    for (auto& e : constraints)
    {
      constraints_.push_back(e);
    }
  }

  Rule(std::initializer_list<Constraint<placeholder> > keep, std::initializer_list<Constraint<placeholder>> consume, 
    body_t body, rule_type type = SIMPAGATION, bool ignore_placeholder_restrictions = false) : body_(body), 
    type_(type), ignore_placeholder(ignore_placeholder_restrictions)
  {
    assert(type == SIMPAGATION);

    for (auto& e : keep)
    {
      constraints_.push_back(e);
    }

    for (auto& e : consume)
    {
      constraints_.push_back(e);
      consumable_constraints_.push_back(e);
    }
  }

private:
  std::vector<Constraint<placeholder> > constraints_; //contains all involved constraints
  body_t body_;
  rule_type type_;

  std::vector<Constraint<placeholder> > consumable_constraints_; //special case for simpagation rules, only those are consumed

  bool ignore_placeholder;
};