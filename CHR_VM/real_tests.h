#pragma once
#include <chrono>
#include <vector>
#include "WorkerCHR.h"
#include "Rule.h"
#include "Constraint.h"
#include <stdint.h>
#include <thread>

static bool factorial(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> fac('x');
  Constraint<placeholder> fac2('y');
  Constraint<placeholder> calc('x');

  std::atomic<size_t> crule1{ 0 };
  std::atomic<size_t> crule2{ 0 };

  Rule rule({ calc }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);
    
    auto x = cs[0]->get(0);

    if (x > 1)
    {
      worker.add_constraint(calc.get_instance(x - 1));
      worker.add_constraint(fac.get_instance(x));
    }
  //  s.print();
  //  printf("\n");
  }, SIMPLIFICATION, true);

  Rule mult({ fac, fac }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);
    
    auto x = cs[0]->get(0);
    auto y = cs[1]->get(0);
    worker.add_constraint(fac.get_instance(x*y));
  //  s.print();
  //  printf("\n");
  }, SIMPLIFICATION, true);

  worker.add_rule(&rule);
  worker.add_rule(&mult);

  worker.add_constraint(calc.get_instance(init));

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel(threads, m);
  auto end   = std::chrono::high_resolution_clock::now();
  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  s.print();

  return true;
}

static bool factorial2(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, uint64_t init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> fac('x');
  Constraint<placeholder> fac2('y');
  Constraint<placeholder> calc('x');

  std::atomic<size_t> crule1{ 0 };
  std::atomic<size_t> crule2{ 0 };

  Rule rule({ calc }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);

    auto x = cs[0]->get(0);

    if (x > 1)
    {
      worker.add_constraint(calc, { x - 1 });
      worker.add_constraint(fac, { x });
    }
    //  s.print();
    //  printf("\n");
  }, SIMPLIFICATION, true);

  Rule mult({ fac, fac }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);

    auto x = cs[0]->get(0);
    auto y = cs[1]->get(0);
    worker.add_constraint(fac, { x*y });
    //  s.print();
    //  printf("\n");
  }, SIMPLIFICATION, true);

  worker.add_rule(&rule);
  worker.add_rule(&mult);

  worker.add_constraint(calc, { init });

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel2(threads, m);
  auto end = std::chrono::high_resolution_clock::now();
  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  s.print2();

  return true;
}

static bool unordered_map(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> insert('x', 'y');
  Constraint<placeholder> map('x', 'y');
  Constraint<placeholder> get('x');

  std::atomic<size_t> crule1{ 0 };
  std::atomic<size_t> crule2{ 0 };

  Rule in({ insert }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);
    worker.add_constraint(map.get_instance(cs[0]->get(0), cs[0]->get(1)));
  }, SIMPLIFICATION);

  Rule out({ get, map }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule2.fetch_add(1);
    auto searched = cs[0]->get(0);
    auto key = cs[1]->get(0);
    auto val = cs[1]->get(1);
    if (searched != key || searched != val)
    {
      printf("searched=%lu, found(key=%lu, value=%lu)\n", cs[0]->get(0), cs[1]->get(0), cs[1]->get(1));
    }
  }, SIMPLIFICATION);

  worker.add_rule(&in);
  worker.add_rule(&out);

  for (uint64_t i = 0; i < size; ++i)
  {
    worker.add_constraint(insert.get_instance(i, i));
    worker.add_constraint(get.get_instance(i));
  }

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  s.print();
  std::cout << "rule1: " << crule1.load() << " rule2: " << crule2.load() << "\n";
  //printf("everythings fine\n");
  return true;
}

static bool unordered_map2(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> insert('x', 'y');
  Constraint<placeholder> map('x', 'y');
  Constraint<placeholder> get('x');

  std::atomic<size_t> crule1{ 0 };
  std::atomic<size_t> crule2{ 0 };

  Rule in({ insert }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);
    worker.add_constraint(map, { cs[0]->get(0), cs[0]->get(1) });
  }, SIMPLIFICATION);

  Rule out({ get, map }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule2.fetch_add(1);
    auto searched = cs[0]->get(0);
    auto key = cs[1]->get(0);
    auto val = cs[1]->get(1);
    if (searched != key || searched != val)
    {
      printf("searched=%lu, found(key=%lu, value=%lu)\n", cs[0]->get(0), cs[1]->get(0), cs[1]->get(1));
    }
  }, SIMPLIFICATION);

  worker.add_rule(&in);
  worker.add_rule(&out);

  for (uint64_t i = 0; i < size; ++i)
  {
    worker.add_constraint(insert, { i, i });
    worker.add_constraint(get, { i });
  }

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel2(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  s.print2();
  std::cout << "rule1: " << crule1.load() << " rule2: " << crule2.load() << "\n";
  //printf("everythings fine\n");
  return true;
}

//geht nicht ohne guards
static bool primefilter(uint32_t threads, mode m, size_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> upto('x');
  Constraint<placeholder> prime('x');

  std::atomic<size_t> crule1{ 0 };
  std::atomic<size_t> crule2{ 0 };

  Rule rule({ upto }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);

    auto x = cs[0]->get(0);

    if (x > 1)
    {
      worker.add_constraint(upto.get_instance(x - 1));
      worker.add_constraint(prime.get_instance(x));
    }

    s.print();
    printf("\n");
  }, SIMPLIFICATION, true);

  Rule filter({ prime, prime }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);

    auto x = cs[0]->get(0);
    auto y = cs[1]->get(0);

    if (y % x == 0)
    {
      //worker.add_constraint(prime.get_instance(x));
      s.print();
      printf("\n");
    }
    s.print();
    printf("\n");
    
  }, SIMPAGATION, true);

  worker.add_rule(&rule);
  worker.add_rule(&filter);

  worker.add_constraint(upto.get_instance(init));

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel(threads, OPTIMISTIC_RESTART);
  auto end = std::chrono::high_resolution_clock::now();
  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  s.print();

  return true;
}

static bool bag(uint32_t threads, mode m, int size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> insert('x');
  Constraint<placeholder> item('y');
  Constraint<placeholder> get('x');

  std::atomic<size_t> crule1{ 0 };
  std::atomic<size_t> crule2{ 0 };

  Rule in({ insert }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);
    worker.add_constraint(item.get_instance(cs[0]->get(0)));
  }, SIMPLIFICATION);

  Rule out({ get, item }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule2.fetch_add(1);
  }, SIMPLIFICATION);

  worker.add_rule(&in);
  worker.add_rule(&out);

  for (uint64_t i = 0; i < size; ++i)
  {
    worker.add_constraint(insert.get_instance(i));
    worker.add_constraint(get.get_instance(i));
  }
  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  if (crule1.load() != crule2.load())
  {
    printf("error! %lu,  %lu\n", crule1.load(), crule2.load());
  }

  //s.print();
  //printf("everythings fine\n");
  return true;
}

static bool bag2(uint32_t threads, mode m, int size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s, false);

  Constraint<placeholder> insert('x');
  Constraint<placeholder> item('y');
  Constraint<placeholder> get('x');

  std::atomic<size_t> crule1{ 0 };
  std::atomic<size_t> crule2{ 0 };

  Rule in({ insert }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);
    worker.add_constraint(item, { cs[0]->get(0) });
    if (crule1.load() % 10000 == 0)
    {
      printf("1 %i\n", crule1.load());
    }
  }, SIMPLIFICATION);

  Rule out({ item },{ get/*, item */}, [&](std::vector<Constraint<uint64_t>* > cs){
    crule2.fetch_add(1);

    if (crule2.load() % 1000 == 0)
    {
      printf("2 %i\n", crule2.load());
    }
  });

  worker.add_rule(&in);
  worker.add_rule(&out);

  for (uint64_t i = 0; i < size; ++i)
  {
    worker.add_constraint(insert, { i });
    worker.add_constraint(get, { i });
  }
  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel2(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  if (crule1.load() != crule2.load())
  {
    printf("error! %lu,  %lu\n", crule1.load(), crule2.load());
  }
  printf("r1 = %i, r2 = %i\n", crule1.load(), crule2.load());
  //s.print2();
  //printf("everythings fine\n");
  return true;
}

static bool accumulate(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> item('x');

  std::atomic<size_t> crule1{ 0 };
  std::atomic<size_t> crule2{ 0 };

  Rule add({ item, item }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);
    worker.add_constraint(item.get_instance(cs[0]->get(0) + cs[1]->get(0)));

  //  s.print();
  //  printf("\n");

  }, SIMPLIFICATION, true);

  worker.add_rule(&add);

  for (uint64_t i = 1; i <= size; ++i)
  {
    worker.add_constraint(item.get_instance(i));
  }

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  s.print();
  return true;
}

static bool accumulate2(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> num('x');

  Rule add({ num, num }, [&](std::vector<Constraint<uint64_t>* > cs){
    worker.add_constraint(num, { cs[0]->get(0) + cs[1]->get(0) });
  //  s.print2();
  //  printf("\n");
  }, SIMPLIFICATION, true);

  worker.add_rule(&add);

  for (uint64_t i = 1; i <= size; ++i)
  {
    worker.add_constraint(num, { i });
  }
  //s.print2();
  //printf("\n");
  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel2(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  s.print2();
  return true;
}

static bool blocksworld(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> block('b');
  Constraint<placeholder> clear('y');
  Constraint<placeholder> on('b', 'x');
  Constraint<placeholder> move('b', 'x', 'y');

  Rule rule({ block, clear, on, move }, [&](std::vector<Constraint<uint64_t>* > cs){
    auto b = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == block.get_name(); });
    auto cl = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == clear.get_name(); });
    auto o = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == on.get_name(); });
    auto m = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == move.get_name(); });
    worker.add_constraint(block.get_instance((*b)->get(0)));
    worker.add_constraint(clear.get_instance((*o)->get(1)));
    worker.add_constraint(on.get_instance((*b)->get(0), (*cl)->get(0)));
  }, SIMPLIFICATION);
  worker.add_rule(&rule);

  for (size_t i = 0; i < size; ++i)
  {
    worker.add_constraint(block.get_instance(i));
    worker.add_constraint(on.get_instance(i, i));
    worker.add_constraint(move.get_instance(i, i, size + i));
    worker.add_constraint(clear.get_instance(size + i));
  }

//  printf("init\n");
//  s.print();
//  printf("\n\n");

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  //s.print();

  return true;
}

static bool blocksworld2(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> block('b');
  Constraint<placeholder> clear('y');
  Constraint<placeholder> on('b', 'x');
  Constraint<placeholder> move('b', 'x', 'y');
  std::atomic<uint64_t> rulecounter{ 0 };

  Rule rule({ block }, {clear, on, move
}, [&](std::vector<Constraint<uint64_t>* > cs){
    auto b = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == block.get_name(); });
    auto cl = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == clear.get_name(); });
    auto o = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == on.get_name(); });
    auto m = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == move.get_name(); });
    //worker.add_constraint(block, { (*b)->get(0) });
    worker.add_constraint(clear, { (*o)->get(1) });
    worker.add_constraint(on, { (*b)->get(0), (*cl)->get(0) });
    rulecounter.fetch_add(1);
  });
  worker.add_rule(&rule);

  for (size_t i = 0; i < size; ++i)
  {
    worker.add_constraint(block, { i });
    worker.add_constraint(on, { i, i });
    worker.add_constraint(move, { i, i, size + i });
    worker.add_constraint(clear, { size + i });
  }

  //s.print2();

  //  printf("init\n");
  //  s.print();
  //  printf("\n\n");

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel2(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  std::cout << rulecounter.load() << "\n";
  //s.print();

  return true;
}

static bool dining_philosophers(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> think('p', 'l', 'r');
  Constraint<placeholder> forkleft('l');
  Constraint<placeholder> forkright('r');

  std::vector<uint64_t> counts(threads);

  Rule eating({ think, forkleft, forkright }, [&](std::vector<Constraint<uint64_t>* > cs){
    auto phil = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == think.get_name(); });
    auto left_fork = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == forkleft.get_name(); });
    auto right_fork = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == forkright.get_name(); });

    ++counts[(*phil)->get(0)];

    worker.add_constraint(forkleft.get_instance((*left_fork)->get(0)));
    worker.add_constraint(forkright.get_instance((*right_fork)->get(0)));
  }, SIMPLIFICATION);
  worker.add_rule(&eating);

  std::vector<uint64_t> perms;

  for (size_t i = 0; i < (threads/2); ++i)
  {
    worker.add_constraint(forkleft.get_instance(i));
    worker.add_constraint(forkright.get_instance(i));
  }

  uint32_t left = (threads / 2) - 1;
  uint32_t right = (threads / 2) - 1;

  for (size_t i = 0; i < size; ++i)
  {
    worker.add_constraint(think.get_instance((i % threads), left, right));

    if (i % 2)
      left = (left + 1) % (threads/2);
    else
      right = (right + 1) % (threads/2);
  }

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel(threads, m);
  auto end = std::chrono::high_resolution_clock::now();
  
  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  s.print();

  for (size_t i = 0; i < counts.size(); ++i)
  {
    printf("phil %lu ate %lu times\n", i, counts[i]);
  }

  return true;
}

static bool dining_philosophers2(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> think('p', 'l', 'r');
  Constraint<placeholder> forkleft('l');
  Constraint<placeholder> forkright('r');

  std::vector<uint64_t> counts(threads);

  Rule eating({ think, forkleft, forkright }, [&](std::vector<Constraint<uint64_t>* > cs){
    auto phil = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == think.get_name(); });
    auto left_fork = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == forkleft.get_name(); });
    auto right_fork = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == forkright.get_name(); });

    ++counts[(*phil)->get(0)];

    worker.add_constraint(forkleft,  { (*left_fork)->get(0)  });
    worker.add_constraint(forkright, { (*right_fork)->get(0) });
  }, SIMPLIFICATION);
  worker.add_rule(&eating);

  std::vector<uint64_t> perms;

  for (size_t i = 0; i < (threads / 2); ++i)
  {
    worker.add_constraint(forkleft, { i });
    worker.add_constraint(forkright, { i });
  }

  uint32_t left = (threads / 2) - 1;
  uint32_t right = (threads / 2) - 1;

  for (size_t i = 0; i < size; ++i)
  {
    worker.add_constraint(think, { (i % threads), left, right });

    if (i % 2)
      left = (left + 1) % (threads / 2);
    else
      right = (right + 1) % (threads / 2);
  }

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel2(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

  s.print();

  for (size_t i = 0; i < counts.size(); ++i)
  {
    printf("phil %lu ate %lu times\n", i, counts[i]);
  }

  return true;
}

static bool producer_consumer(uint32_t threads, mode m, uint64_t size, size_t runs, std::vector<uint64_t>& times, int init)
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> producer('x');
  Constraint<placeholder> consumer('y');
  Constraint<placeholder> item('z');
  const uint64_t max = size;
  std::atomic<uint32_t> rulecounter{ 0 };

  Rule consume({ consumer, item }, [&](std::vector<Constraint<uint64_t>* > cs){   
    if (rulecounter.load() < max)
    {
      worker.add_constraint(consumer.get_instance(cs[0]->get(0)));
      rulecounter.fetch_add(1);
    }
  }, SIMPLIFICATION);
  Rule produce({ producer }, [&](std::vector<Constraint<uint64_t>* > cs){
    if (rulecounter.load() < max)
    {
      worker.add_constraint(item.get_instance(cs[0]->get(0)));
      worker.add_constraint(producer.get_instance(cs[0]->get(0)));
    }
  }, SIMPLIFICATION);

  worker.add_rule(&produce);
  worker.add_rule(&consume);

  for (size_t i = 0; i < threads; ++i)
  {
    if (i%2)
      worker.add_constraint(producer.get_instance(i));
    else
      worker.add_constraint(consumer.get_instance(i));
  }

  auto start = std::chrono::high_resolution_clock::now();
  worker.run_parallel(threads, m);
  auto end = std::chrono::high_resolution_clock::now();

  times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
  std::cout << "rule: " << rulecounter.load() << "\n";
  s.print();
  return true;
}