#include "Rule.h"
#include "Constraint.h"
#include "placeholder.h"

#pragma once

struct MatchChecklist
{
  MatchChecklist(Rule* r) : rule_(r)
  {
    for (size_t i = 0; i < rule_->constraints_.size(); ++i)
    {
      constraints_.push_back({ &rule_->constraints_[i], false });
    }

    if (r->type_ == SIMPAGATION)
    {
      for (auto& e : rule_->consumable_constraints_)
      {
        consumable_constraints_.push_back(&e);
        consumable_stati_placeholder_.push_back(&e);
      }
    }
  }

  void operator=(const MatchChecklist& list)
  {
    constraints_.clear();
    consumable_constraints_.clear();
    used_constraints_.clear();
    placeholder_values_.clear();
    consumable_.clear();

    rule_ = list.rule_;
    for (auto& e : list.constraints_)
      constraints_.push_back(e);
    for (auto& e : list.consumable_constraints_)
      consumable_constraints_.push_back(e);
    for (auto& e : list.used_constraints_)
      used_constraints_.push_back(e);
    for (auto& e : list.placeholder_values_)
      placeholder_values_.insert(e);
    for (auto& e : list.consumable_)
      consumable_.push_back(e);
  }

  bool is_match(Constraint<uint64_t>* c)
  {
    for (auto& constraint : constraints_)
    {
      if (constraint.first->name_ == c->name_ && constraint.second == false) // check if given constraint is in the rule
      {
        std::vector<std::pair<placeholder, uint64_t> > to_add;

        if (!rule_->ignore_placeholder)
        {
          for (size_t i = 0; i < constraint.first->placeholders_.size(); ++i)
          {
            auto pvalue = placeholder_values_.find(constraint.first->placeholders_[i]);
            if (pvalue != placeholder_values_.end())
            {
              if (pvalue->second != c->args_[i])
                return false;
            }
            else
              to_add.push_back({ constraint.first->placeholders_[i], c->args_[i] });
          }
        }

        //all placeholders are matching the constraint. add all new ones to the set
        for (auto& p : to_add)
        {
          placeholder_values_.insert(p);
        }

        constraint.second = true;
        used_constraints_.push_back(c);

        if (rule_->type_ == SIMPAGATION)
        {
          auto it = std::find_if(std::begin(consumable_constraints_), std::end(consumable_constraints_), [&](Constraint<placeholder>* p1){
            return p1->get_name() == constraint.first->get_name();
          });

          if (it != std::end(consumable_constraints_))
          {
            consumable_.push_back(c);
            consumable_constraints_.erase(it);
          }
        }

        std::sort(used_constraints_.begin(), used_constraints_.end(), [&](Constraint<uint64_t>* a, Constraint<uint64_t>* b)
        {
          return a->get_id() < b->get_id();
        });

        return true;
      }
    }
    return false;
  }

  bool add(Constraint<uint64_t>* c)
  {
    for (auto& constraint : constraints_)
    {
      if (constraint.first->name_ == c->name_ && constraint.second == false) // check if given constraint is in the rule
      {
        for (size_t i = 0; i < constraint.first->placeholders_.size(); ++i)
        {
          placeholder_values_.insert({ constraint.first->placeholders_[i], c->args_[i] });
        }
        constraint.second = true;
        used_constraints_.push_back(c);

        if (rule_->type_ == SIMPAGATION)
        {
          auto it = std::find_if(std::begin(consumable_constraints_), std::end(consumable_constraints_), [&](Constraint<placeholder>* p1){
            return p1->get_name() == constraint.first->get_name();
          });

          if (it != std::end(consumable_constraints_))
          {
            consumable_.push_back(c);
            consumable_constraints_.erase(it);
          }
        }

        std::sort(used_constraints_.begin(), used_constraints_.end(), [&](Constraint<uint64_t>* a, Constraint<uint64_t>* b)
        {
          return a->get_id() < b->get_id();
        });

        return true;
      }
    }
    return false;
  }

  bool convenient(const Constraint<uint64_t>* c)
  {
    for (auto& constraint : constraints_)
    {
      if (constraint.first->name_ == c->name_ && constraint.second == false) // check if given constraint is in the rule
      {
        if (!rule_->ignore_placeholder)
        {
          for (size_t i = 0; i < constraint.first->placeholders_.size(); ++i)
          {
            auto pvalue = placeholder_values_.find(constraint.first->placeholders_[i]);
            if (pvalue != placeholder_values_.end())
            {
              if (pvalue->second != c->args_[i])
                return false;
            }
          }
        }
        return true;
      }
    }
    return false;
  }

  bool is_complete()
  {
    for (auto& c : constraints_)
    {
      if (c.second == false)
        return false;
    }
    return true;
  }

  struct hint
  {
    //std::string name;
    uint32_t name;
    std::vector<std::pair<bool, uint64_t> > arg_hints_;
    bool args;

    hint() : args(false)
    {}
  };

  hint get_hint()
  {
    auto it = std::find_if(std::begin(constraints_), std::end(constraints_), [&](const std::pair<Constraint<placeholder>*, bool>& c) {
      if (c.second == false)
      {
        for (size_t i = 0; i < c.first->placeholders_.size(); ++i)
        {
          auto arg = placeholder_values_.find(c.first->placeholders_[i]);
          if (arg != placeholder_values_.end())
            return true;
        }
      }
      return false;
    });

    for (auto c = (it != constraints_.end()) ? it : constraints_.begin(); c != constraints_.end(); ++c)
    {
      if (c->second == false)
      {
        hint h;
        h.name = c->first->name_;

        if (!rule_->ignore_placeholder)
        {
          for (size_t i = 0; i < c->first->placeholders_.size(); ++i)
          {
            auto arg = placeholder_values_.find(c->first->placeholders_[i]);

            if (arg != std::end(placeholder_values_))
            {
              h.arg_hints_.push_back({ true, arg->second });
              h.args = true;
            }
            else
              h.arg_hints_.push_back({ false, 0 });
          }
        }

        return h;
      }
    }
    return hint{};
  }

  void reset()
  {
    used_constraints_.clear();
    placeholder_values_.clear();
    consumable_.clear();
    for (auto& e : constraints_)
      e.second = false;
  }

  bool remove_constraint(Constraint<uint64_t>* c)
  {
    //backup
    std::vector<Constraint<uint64_t>* > constraintbackup;
    for (auto& e : used_constraints_)
    {
      if (e->id_ != c->id_)
        constraintbackup.push_back(e);
    }

    //reset
    used_constraints_.clear();
    placeholder_values_.clear();
    consumable_.clear();
    for (auto& e : constraints_)
      e.second = false;

    //replay
    for (auto& e : constraintbackup)
    {
      if (!is_match(e))
        return false;
    }
    return true;
  }

  bool convenient(Store::status* s)
  {
    return convenient(s->constraint);
  }

  bool add(Store::status* s)
  {
    if (add(s->constraint))
    {
      used_stati_.push_back(s);

      if (rule_->type_ == SIMPAGATION)
      {
        auto it = std::find_if(std::begin(consumable_stati_placeholder_), 
          std::end(consumable_stati_placeholder_), [&](Constraint<placeholder>* p1){
          return p1->get_name() == s->constraint->get_name();
        });

        if (it != std::end(consumable_stati_placeholder_))
        {
          consumable_stati_.push_back(s);
          consumable_stati_placeholder_.erase(it);
        }
        else
          keep_stati_.push_back(s);
      }

      return true;
    }
    return false;
  }

  bool is_match(Store::status* s)
  {
    if (is_match(s->constraint))
    {
      used_stati_.push_back(s);

      if (rule_->type_ == SIMPAGATION)
      {
        auto it = std::find_if(std::begin(consumable_stati_placeholder_), 
          std::end(consumable_stati_placeholder_), [&](Constraint<placeholder>* p1){
          return p1->get_name() == s->constraint->get_name();
        });

        if (it != std::end(consumable_stati_placeholder_))
        {
          consumable_stati_.push_back(s);
          consumable_stati_placeholder_.erase(it);
        }
        else
          keep_stati_.push_back(s);
      }

      return true;
    }
    return false;
  }

  bool remove_constraint(Store::status* s)
  {
    if (remove_constraint(s->constraint))
    {
      for (auto it = std::begin(used_stati_); it != std::end(used_stati_); ++it)
      {
        if ((*(*it)->constraint) == (*s->constraint))
        {
          it = used_stati_.erase(it);
          break;
        }
      }
      if (rule_->type_ == SIMPAGATION)
      {
        for (auto it = std::begin(consumable_stati_); it != std::end(consumable_stati_); ++it)
        {
          if ((*(*it)->constraint) == (*s->constraint))
          {
            it = consumable_stati_.erase(it);
            break;
          }
        }
      }
      return true;
    }
    return false;
  }

  std::vector<Constraint<uint64_t>* > get_rule_ordered()
  {
    using namespace std;
    vector<Constraint<uint64_t>* > ret(used_constraints_.size());
    for (auto& c : used_constraints_)
    {
      for (auto& it = begin(constraints_); it != end(constraints_); ++it)
      {
        if ((*it).first->name_ == c->name_)
        {
          auto idx = std::distance(begin(constraints_), it);
          if (ret[idx] == nullptr)
          {
            ret[idx] = c;
            break;
          }
        }
      }
    }
    return ret;
  }

  Rule* rule_;
  std::vector<std::pair<Constraint<placeholder>*, bool> > constraints_;
  std::vector <Constraint<placeholder>* > consumable_constraints_;
  std::vector<Constraint<uint64_t>* > used_constraints_;
  std::unordered_map<char, uint64_t> placeholder_values_;
  std::vector<Constraint<uint64_t>* > consumable_;

  std::vector <Constraint<placeholder>* > consumable_stati_placeholder_;
  std::vector<Store::status*> used_stati_;
  std::vector<Store::status*> consumable_stati_;
  std::vector<Store::status*> keep_stati_;

  Store::status* last_add_;
};