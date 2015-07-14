#include "Store.h"
#include "MurmurHash3.h"
#include <unordered_set>
#include <stdarg.h>

Store::Store(std::vector<Constraint<uint64_t>* >& v)
{
  for (auto& it : v)
  {
    insert(it);
  }
}

Store::Store()
{

}

Store::Store(std::initializer_list<Constraint<uint64_t>*> l)
{
  for (auto& it : l)
  {
    insert(it);
  }
}

Store::~Store()
{
  for (auto& e : store_)
  {
    for (auto& x : e.second)
    {
      delete x;
    }
  }

  for (auto& e : name_hashmap_)
  {
    for (auto& x : e.second)
    {
      delete x->constraint;
      delete x;
    }
  }
}

void Store::rehash()
{
  store_.rehash();

  store2_.rehash();
  for (auto& e : store2_)
  {
    e.second.rehash();
  }
}

void Store::print_sizes()
{
  /*printf("old\n");
  
  for (auto& e : store_arguments_)
  {
    if (linewidth == 3)
      printf("\n");

    printf("%i ", e.second.size());
    linewidth = (linewidth + 1) % 4;
  }
  printf("\n\n");*/
  int linewidth = 0;
  printf("new\n");
  linewidth = 0;
  //printf("count buckets_first: %i\n", store2_.bucket_count());

  for (auto& e : store2_)
  {
    //printf("\nbucketcount = %i\n", e.second.bucket_count());
    for (auto& i : e.second)
    {
      if (linewidth == 39)
        printf("\n");

      printf("%lu ", i.second.size());
      linewidth = (linewidth + 1) % 40;
    } 
  }
  printf("\n\n");
}

void Store::insert(Constraint<uint64_t>* c)
{
  {
    store_map::accessor a;
    store_.insert(a, c->name_);
    a->second.push_back(c);
  }

  auto add_constraint = [&](uint64_t key) {
    store_map2::accessor a;
    store2_.insert(a, c->name_);
    store_map::accessor b;
    a->second.insert(b, key);
    b->second.push_back(c);
  };

  for (size_t i = 0; i < c->args_.size(); ++i)
  {
    auto key = murmurhash3_64bit(c->args_[i]);
    add_constraint(key);

    for (size_t j = i + 1; j < c->args_.size(); ++j)
    {
      auto key = murmurhash3_64bit(c->args_[i]) ^ murmurhash3_64bit(c->args_[j]);
      add_constraint(key);

      for (size_t k = j + 1; k < c->args_.size(); ++k)
      {
        auto key = murmurhash3_64bit(c->args_[i]) ^ murmurhash3_64bit(c->args_[j]) ^ murmurhash3_64bit(c->args_[k]);
        add_constraint(key);

        for (size_t l = k + 1; l < c->args_.size(); ++l)
        {
          auto key = murmurhash3_64bit(c->args_[i]) ^ murmurhash3_64bit(c->args_[j]) ^ murmurhash3_64bit(c->args_[k]) ^ murmurhash3_64bit(c->args_[l]);
          add_constraint(key);
        }
      }
    }
  }
}

Store::status* Store::insert_counted(Constraint<placeholder>& pc, std::vector<uint64_t> args, modi m)
{
  status* duplicate = nullptr;
  status* new_status = nullptr;
  //std::lock_guard<std::mutex> guard(mtx_);

  {   
    //check for duplicates (faster)
    auto key = murmurhash3_64bit(args[0]);

    map_first_level::accessor a;
    arg_hashmap_.find(a, pc.get_name());

    if (!a.empty())
    {
      map_second_level::accessor b;
      a->second.find(b, key);
      
      if (!b.empty())
      {
        for (auto& e : b->second)
        {
          if (e->constraint->is_same(pc.get_name(), args))
          {
            duplicate = e;
            duplicate->add_pending();
            break;
          }
        }
      }
    }   
  }

  if (!duplicate)
  {
    map_second_level::accessor a;
    name_hashmap_.insert(a, pc.get_name());

    new_status = new status(pc.get_instance(args), m);
    a->second.push_back(new_status);
  }
  
  

  if (!duplicate)
  {
    auto add_constraint = [&](uint64_t key, map_first_level& arg_map) {
      map_first_level::accessor a;
      arg_map.insert(a, new_status->constraint->get_name());
      map_second_level::accessor b;
      a->second.insert(b, key);
      for (auto& e : b->second)
      {
        if (e->constraint->get_id() == new_status->constraint->get_id())
          return;
      }
      b->second.push_back(new_status);
    };

    for (size_t i = 0; i < new_status->constraint->args_.size(); ++i)
    {
      auto key = murmurhash3_64bit(new_status->constraint->args_[i]);
      add_constraint(key, arg_hashmap_);

      for (size_t j = i + 1; j < new_status->constraint->args_.size(); ++j)
      {
        auto key = murmurhash3_64bit(new_status->constraint->args_[i]) + murmurhash3_64bit(new_status->constraint->args_[j]);
        add_constraint(key, arg_hashmap_);

        for (size_t k = j + 1; k < new_status->constraint->args_.size(); ++k)
        {
          auto key = murmurhash3_64bit(new_status->constraint->args_[i]) + 
            murmurhash3_64bit(new_status->constraint->args_[j]) +
            murmurhash3_64bit(new_status->constraint->args_[k]);
          add_constraint(key, arg_hashmap_);

          for (size_t l = k + 1; l < new_status->constraint->args_.size(); ++l)
          {
            auto key = murmurhash3_64bit(new_status->constraint->args_[i]) + 
              murmurhash3_64bit(new_status->constraint->args_[j]) + 
              murmurhash3_64bit(new_status->constraint->args_[k]) + 
              murmurhash3_64bit(new_status->constraint->args_[l]);
            add_constraint(key, arg_hashmap_);
          }
        }
      }
    }
  }

  if (duplicate)
    return duplicate;
  else
    return new_status;
}