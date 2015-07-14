#include <iostream>

#include "Constraint.h"
#include "Store.h"
#include "WorkerCHR.h"
#include "Rule_Types.h"
#include "Match_Checklist.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <vector>
#include <omp.h>
#include <sstream>
#include <algorithm>
#include <numeric>

#include "ID_Manager.h"
#include "Stats.h"

#include <fstream>
#include "real_tests.h"

#include "ThreadPool.h"

template<typename T>
void test_sum(T& output, mode m, int threads = std::thread::hardware_concurrency())
{
  printf("test sum\n");
  const int size = 100000;
  Store s;
  WorkerCHR worker(&s);
  
  Constraint<placeholder> num('x');
  Constraint<placeholder> numy = num.sibling({ 'y' });

  Rule rule({ num, numy }, [&](std::vector<Constraint<uint64_t>* > cs){
    worker.async(num, { cs[0]->get(0) + cs[1]->get(0) }, m);
  }, SIMPLIFICATION);

  worker.add_rule(&rule);

  auto start = std::chrono::high_resolution_clock::now();

  omp_set_num_threads(threads);

#pragma omp parallel for
  for (int i = 1; i <= size; ++i)
  {
    worker.async(num, { uint64_t(i) }, m);
  }

  auto res = worker.get();

  auto end = std::chrono::high_resolution_clock::now();

  output << "sum " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

  for (auto& e : res)
  {
    printf("Rulename = %i, value = %s, id = %i\n", e->get_name(),
      e->str().c_str(), e->get_id());
  }
  //s.print2();
}

template<typename T>
void producer_consumer(T& output, mode m, int threads = std::thread::hardware_concurrency())
{
  printf("test producer consumer\n");
  omp_set_nested(1);
  const int size = 100000;
  Store s;
  WorkerCHR worker(&s);

  std::atomic<uint64_t> rulec{ 0 };

  Constraint<placeholder> get('x');
  Constraint<placeholder> put('y');

  Rule rule({ get, put }, [&](std::vector<Constraint<uint64_t>* > cs){
    rulec.fetch_add(1);
  });
  worker.add_rule(&rule);

  auto start = std::chrono::high_resolution_clock::now();

#pragma omp parallel num_threads(2)
  {
    if (omp_get_thread_num() == 0)
    {
#pragma omp parallel for num_threads(threads/2)
      for (int i = 0; i < size; ++i)
      {
        worker.async(put, { uint64_t(i) }, m);
      }
    }
    else
    {
#pragma omp parallel for num_threads(threads/2)
      for (int i = 0; i < size; ++i)
      {
        worker.sync(get, { uint64_t(42) }, m);
      }
    }
  }

  worker.wait();

  auto end = std::chrono::high_resolution_clock::now();
  output << "producer consumer " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

  std::cout << rulec.load() << "\n";
}

template<typename T>
void test_mutex(T& output, mode m, int threads = std::thread::hardware_concurrency())
{
  printf("test mutex\n");
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> acquire('x');
  Constraint<placeholder> release('x');

  std::atomic<uint32_t> rule_counter{ 0 };

  Rule lock({ acquire, release }, [&](std::vector<Constraint<uint64_t>* > cs){
    rule_counter.fetch_add(1);
  //  s.print2();
  }, SIMPLIFICATION, true);

  worker.add_rule(&lock);

  worker.async(release, { 1 }, m);

  omp_set_num_threads(threads);

  auto start = std::chrono::high_resolution_clock::now();

  int big_n = 0;

#pragma omp parallel for
  for (int i = 1; i <= 100000; ++i)
  {
    worker.sync(acquire, { 1/*uint64_t(i)*/ }, m);

    int n = 0;
    for (size_t j = 0; j < 1000; ++j)
      n += rand()%10;

    big_n += n;
    /*if (!(rule_counter.load() % 10000))
      printf("%i\n", rule_counter.load());*/
    /*printf("%i", rule_counter.load());
    if (i % 20)
      printf(", ");
    else
      printf("\n");*/

    worker.async(release, { 1/*uint64_t(i) */ }, m);

  }
  
  auto res = worker.get();

  auto end = std::chrono::high_resolution_clock::now();

  std::cout << "big_n = " << big_n << " rule counter = " << rule_counter.load() << "\n";

  output << "mutex " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";
}

template<typename T>
static bool test_unordered_map(T& output, mode m, int threads = std::thread::hardware_concurrency())
{
  printf("test unordered map\n");
  Store s;
  WorkerCHR worker(&s);

  const int size = 100000;

  Constraint<placeholder> insert('x', 'y');
  Constraint<placeholder> map('x', 'y');
  Constraint<placeholder> get('x');

  std::atomic<size_t> crule1{ 0 };
  std::atomic<size_t> crule2{ 0 };

  Rule in({ insert }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule1.fetch_add(1);
    worker.async(map, { cs[0]->get(0), cs[0]->get(1) }, m);
  }, SIMPLIFICATION);

  Rule out({ map },{ get/*, map*/ }, [&](std::vector<Constraint<uint64_t>* > cs){
    crule2.fetch_add(1);
    auto searched = cs[1]->get(0);
    auto key = cs[0]->get(0);
    auto val = cs[0]->get(1);
    //printf("searched=%i, key=%i, val=%i\n", searched, key, val);
    if (searched != key || searched != val)
    {
      printf("searched=%lu, found(key=%lu, value=%lu)\n", cs[0]->get(0), cs[1]->get(0), cs[1]->get(1));
    }
  });

  worker.add_rule(&in);
  worker.add_rule(&out);

  auto add = [&](uint64_t i) {
    worker.async(insert, { i, i }, m);
  };
  auto pull = [&](uint64_t i) {
    worker.sync(get, { i }, m);
  };

  auto start = std::chrono::high_resolution_clock::now();

  std::vector<std::thread> putter;
  std::vector<std::thread> getter;

  omp_set_num_threads(threads);

  auto s1 = std::chrono::high_resolution_clock::now();
#pragma omp parallel for
  for (int i = 0; i < size; ++i)
    worker.sync(insert, { uint64_t(i), uint64_t(i) }, m);
  /*putter.push_back(std::thread([&]() {
    for (int i = 0; i < 25000; ++i)
      add(uint64_t(i));
  }));
  putter.push_back(std::thread([&]() {
    for (int i = 25000; i < 50000; ++i)
      add(uint64_t(i));
  }));
  putter.push_back(std::thread([&]() {
    for (int i = 50000; i < 75000; ++i)
      add(uint64_t(i));
  }));
  putter.push_back(std::thread([&]() {
    for (int i = 75000; i < 100000; ++i)
      add(uint64_t(i));
  }));*/

  worker.wait();
  /*for (auto& p : putter)
    p.join();*/

  auto e1 = std::chrono::high_resolution_clock::now();
  output << "write " <<  std::chrono::duration_cast<std::chrono::milliseconds>(e1 - s1).count() << " ms\n";

  auto s2 = std::chrono::high_resolution_clock::now();
#pragma omp parallel for
  for (int i = 0; i < size; ++i)
    worker.sync(get, { uint64_t(i) }, m);

  worker.wait();
  /*for (auto& g : getter)
    g.join();*/
  auto e2 = std::chrono::high_resolution_clock::now();
  output << "read " << std::chrono::duration_cast<std::chrono::milliseconds>(e2 - s2).count() << " ms\n";

  //for (int i = 0; i < (threads / 2); ++i)
  //{
  //  putter.push_back(std::thread([=,&add]{
  //    //for (uint64_t i = 0; i < (100000/(threads/2)); ++i)
  //    for (uint64_t j = i * 2500; j < (i + 1) * 2500; ++j)
  //      add(j);
  //  }));
  //}

  //for (int i = 0; i < (threads / 2); ++i)
  //{
  //  getter.push_back(std::thread([=, &pull]{
  //    //for (uint64_t i = 0; i < (100000 / (threads / 2)); ++i)
  //    for (uint64_t j = i * 2500; j < (i + 1) * 2500; ++j)
  //      pull(j);
  //  }));
  //}  

  /*worker.wait();

  for (auto& p : putter)
    p.join();

  for (auto& g : getter)
    g.join();*/
 
  auto end = std::chrono::high_resolution_clock::now();

  output << "unordered map " <<  std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

  s.print();
  std::cout << "rule1: " << crule1.load() << " rule2: " << crule2.load() << "\n";
  //printf("everythings fine\n");
  return true;
}

template<typename T>
static bool test_blocksworld(T& output, mode m, int threads = std::thread::hardware_concurrency())
{
  printf("test blocksworld\n");
  Store s;
  WorkerCHR worker(&s);

  const auto size = 100000;

  Constraint<placeholder> block('b');
  Constraint<placeholder> clear('y');
  Constraint<placeholder> on('b', 'x');
  Constraint<placeholder> move('b', 'x', 'y');
  std::atomic<uint64_t> rulecounter{ 0 };

  Rule rule(/*{ block, clear, on, move }*/{ block }, { clear, on, move }, [&](std::vector<Constraint<uint64_t>* > cs){
    auto b = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == block.get_name(); });
    auto cl = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == clear.get_name(); });
    auto o = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == on.get_name(); });
    auto mo = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == move.get_name(); });
  //  worker.add_constraint(block, { (*b)->get(0) });
    worker.async(clear, { (*o)->get(1) }, m);
    worker.async(on, { (*b)->get(0), (*cl)->get(0) }, m);
    rulecounter.fetch_add(1);
  }, SIMPAGATION);
  worker.add_rule(&rule);

  omp_set_num_threads(threads);
  auto start = std::chrono::high_resolution_clock::now();
  
#pragma omp parallel for
  for (int i = 0; i < size; ++i)
  {
    uint64_t x = uint64_t(i);
    worker.async(block, { x }, m);
    worker.async(on, { x, x }, m);
    worker.async(clear, { size + x }, m);
    worker.async(move, { x, x, 1000 + x }, m);
  }

  auto end = std::chrono::high_resolution_clock::now();

  /*std::thread blockt([&]()
  {
    for (size_t i = 0; i < size; ++i)
    {
      worker.async(block, { i }, m);
    }
  });

  std::thread ont([&]()
  {
    for (size_t i = 0; i < size; ++i)
    {
      worker.async(on, { i, i }, m);
    }
  });

  omp_set_num_threads(threads-2);
#pragma omp parallel for
  for (int i = 0; i < size; ++i)
  {
    auto x = uint64_t(i);
    worker.sync(move, { x, x, size + x }, m);
  }*/

  worker.wait();
  /*ont.join();
  blockt.join();*/
  
  

  output << "blocksworld " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms \n";
  std::cout << rulecounter.load() << "\n";
  //s.print();

  return true;
}

template<typename T>
static bool test_dining_philosophers(T& output, mode m, int threads = std::thread::hardware_concurrency())
{
  printf("test philos\n");
  Store s;
  WorkerCHR worker(&s);

  const auto size = 100000;

  Constraint<placeholder> think('p', 'l', 'r');
  Constraint<placeholder> forkleft('l');
  Constraint<placeholder> forkright('r');

  std::vector<std::atomic<uint64_t>> counts(threads);

  Rule eating({ think, forkleft, forkright }, [&](std::vector<Constraint<uint64_t>* > cs){
    auto phil = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == think.get_name(); });
    auto left_fork = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == forkleft.get_name(); });
    auto right_fork = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* cc) { return cc->get_name() == forkright.get_name(); });

    ++counts[(*phil)->get(0)];

    /*if (counts[(*phil)->get(0)] % 100 == 0)
    {
      printf("phil %i ate %i times\n", (*phil)->get(0), counts[(*phil)->get(0)].load());
    }*/

    worker.async(forkleft, { (*left_fork)->get(0) }, m);
    worker.async(forkright, { (*right_fork)->get(0) }, m);
  }, SIMPLIFICATION);
  worker.add_rule(&eating);

  for (size_t i = 0; i < (threads / 2); ++i)
  {
    worker.add_constraint(forkleft, { i });
    worker.add_constraint(forkright, { i });
  }

  std::atomic<uint32_t> left ( (threads / 2) - 1 );
  std::atomic<uint32_t> right ( (threads / 2) - 1 );

  omp_set_num_threads(threads);

  auto start = std::chrono::high_resolution_clock::now();

#pragma omp parallel for
  for (int i = 0; i < size; ++i)
  {
    auto x = uint64_t(i);
    worker.sync(think, { (x % threads), left, right }, m);

    if (i % 2)
      left.store((left.load() + 1) % (threads / 2));
    else
      right.store((right.load() + 1) % (threads / 2));
  }

  worker.wait();

  auto end = std::chrono::high_resolution_clock::now();

  output << "philos " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

  s.print();

  for (size_t i = 0; i < counts.size(); ++i)
  {
    printf("phil %lu ate %lu times\n", i, counts[i]);
  }

  return true;
}

//template<typename T>
//static bool test_feistel_cipher2(T& output, mode m, int threads = std::thread::hardware_concurrency())
//{
//  printf("test feistel cipher\n");
//  Store s;
//  WorkerCHR worker(&s);
//
//  const auto size = 10;
//
//  Constraint<placeholder> block('x', 'p');
//  Constraint<placeholder> block2 = block.sibling({ 'y', 'q' });
//  Constraint<placeholder> enc('y', 'i', 'u', 'q');
//  Constraint<placeholder> encf('x', 'i', 'u', 'p');
//  Constraint<placeholder> enc2 = enc.sibling({ 'a', 'i', 'v', 'r' });
//  Constraint<placeholder> encf2 = encf.sibling({'b', 'i', 'v', 's'});
//
//
//  return true;
//}

template<typename T>
static bool test_feistel_cipher(T& output, mode m, int threads = std::thread::hardware_concurrency())
{
  printf("test feistel cipher\n");
  Store s;
  WorkerCHR worker(&s);

  const auto size = 100000;

  Constraint<placeholder> block('x', 'p');
  Constraint<placeholder> block2 = block.sibling({ 'y', 'q' });
  Constraint<placeholder> enc('y', 'i', 'u', 'q');
  Constraint<placeholder> encf('x', 'i', 'u', 'p');
  Constraint<placeholder> enc2 = enc.sibling({ 'a', 'i', 'v', 'r' });
  Constraint<placeholder> encf2 = encf.sibling({'b', 'i', 'v', 's'});
  std::atomic<uint64_t> ids{ 0 };
  tbb::concurrent_vector<Constraint<uint64_t>* > encrypted_file;
  encrypted_file.reserve(4 * size);
  std::vector<uint64_t> file(size);

  Rule encrypt({ block, block2 }, [&](std::vector<Constraint<uint64_t>* > cs){
    auto x1 = cs[0]->get(0)  & 0xFFFFFFFF;
    auto y1 = cs[1]->get(0) & 0xFFFFFFFF;
    auto x2 = (cs[0]->get(0) & 0xFFFFFFFF00000000) >> 32;   
    auto y2 = (cs[1]->get(0) & 0xFFFFFFFF00000000) >> 32;
   // printf("enc %i, %i\n", x1, y1);
    uint64_t newX1 = x1 ^ (/*f(y)*/ y1 ^ 0x888888FF1FF88888);
    uint64_t newX2 = x2 ^ (y2 ^ 0x888888FF1FF88888);
    auto id = ids++;

    /*if (!(ids % 100))
      printf("encrypt id=%i\n", id);*/

    worker.async(enc, { y1, id, 0, cs[1]->get(1) }, OPTIMISTIC_RESTART);
    worker.async(encf, { newX1, id, 0, cs[0]->get(1) }, OPTIMISTIC_RESTART);
    worker.async(enc, { y2, id, 1, cs[1]->get(1) }, OPTIMISTIC_RESTART);
    worker.async(encf, { newX2, id, 1, cs[0]->get(1) }, OPTIMISTIC_RESTART);
  }, SIMPLIFICATION);

  std::atomic<uint64_t> conc_counter{ 0 };

  Rule concatenate({ enc, encf, enc2, encf2 }, [&](std::vector<Constraint<uint64_t>* > cs){
    auto x = cs[0]->get(0); auto y = cs[1]->get(0);
    
    auto bla = conc_counter++;

    /*if (!(conc_counter % 100))
      printf("conc count=%i\n", bla);*/

    /*if (!(bla%1000))
      s.print2();*/

    

    //printf("conc %i, %i\n", x, y);
    for (auto& e : cs)
    {
      encrypted_file.push_back(e);
    }
    //encrypted_file.push_back(cs[0]);
    //encrypted_file.push_back(cs[1]);
  }, SIMPLIFICATION);

  Rule decrypt({ encf, enc, encf2, enc2 }, [&](std::vector<Constraint<uint64_t>* > cs) {
    auto y1 = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* c) { return c->get_name() == enc.get_name() && c->get(2) == 0; });
    auto x1 = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* c) { return c->get_name() == encf.get_name() && c->get(2) == 0; });
    auto y2 = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* c) { return c->get_name() == enc.get_name() && c->get(2) == 1; });
    auto x2 = std::find_if(std::begin(cs), std::end(cs), [&](Constraint<uint64_t>* c) { return c->get_name() == encf.get_name() && c->get(2) == 1; });

    auto restoredX1 = (*x1)->get(0) ^ ((*y1)->get(0) ^ 0x888888FF1FF88888);
    auto restoredX2 = (*x2)->get(0) ^ ((*y2)->get(0) ^ 0x888888FF1FF88888);

    uint64_t restoredBlock1 = (restoredX2 << 32) | restoredX1;
    uint64_t restoredBlock2 = ((*y2)->get(0) << 32) | (*y1)->get(0);

    //auto y = cs[1]->get(0);
   // auto x = cs[0]->get(0) ^ (y ^ 0x888888FF1FF88888);
   // file[cs[0]->get(2)] = restoredBlock1;
   // file[cs[1]->get(2)] = restoredBlock2;
    file[(*x1)->get(3)] = restoredBlock1;
    file[(*y1)->get(3)] = restoredBlock2;
  }, SIMPLIFICATION);

  worker.add_rule(&encrypt);
  worker.add_rule(&concatenate);
  //worker.add_rule(&decrypt);

  omp_set_num_threads(threads);

  auto start = std::chrono::high_resolution_clock::now();

#pragma omp parallel for
  for (int i = 0; i < size; ++i)
  {
    worker.async(block, { uint64_t(i) + 1, uint64_t(i) }, PESSIMISTIC_RESTART);
  }

  worker.get();

  auto end = std::chrono::high_resolution_clock::now();
  output << "encryption: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

  system("pause");

  //decryption
  {
    Store s;
    WorkerCHR worker(&s);
    worker.add_rule(&decrypt);

    auto start = std::chrono::high_resolution_clock::now();

#pragma omp parallel for
    for (int i = 0; i < encrypted_file.size(); ++i)
    {
      auto e = encrypted_file[i];
      worker.async(*e->get_template(), { e->get(0), e->get(1), e->get(2), e->get(3) }, PESSIMISTIC_RESTART);
    }

    /*for (auto& e : encrypted_file)
    {
      worker.async(*e->get_template(), { e->get(0), e->get(1), e->get(2), e->get(3) }, OPTIMISTIC_RESTART);
    }*/

    worker.get();

    auto end = std::chrono::high_resolution_clock::now();
    output << "decryption: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";
    std::cout << file.size() << "\n";
    for (auto i = 0; i < file.size(); ++i)
    {
      if ((i + 1) != file[i])
        printf("error\n");
    }

    
  }

  return true;
}

void error_case()
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> a('x');
  Constraint<placeholder> b('x', 'y');
  Constraint<placeholder> c('y');

  Rule rule({ a, b, c }, [&](std::vector<Constraint<uint64_t>*> cs) {
    printf("yeah rule triggered\n");
  });

  worker.add_rule(&rule);

  worker.async(c, { 4 }, OPTIMISTIC_RESTART);
  system("pause");
  s.print2();
  system("pause");
  worker.async(b, { 2, 3 }, OPTIMISTIC_RESTART);
  system("pause");
  s.print2();
  system("pause");
  worker.async(b, { 2, 4 }, OPTIMISTIC_RESTART);
  system("pause");
  s.print2();
  system("pause");
  worker.async(a, { 2 }, OPTIMISTIC_RESTART);
  system("pause");
  s.print2();
  system("pause");

  worker.get();

  system("pause");
}

template<typename T>
static bool test_barrier(T& output, mode m, int threads = std::thread::hardware_concurrency())
{
  Store s;
  WorkerCHR worker(&s);

  Constraint<placeholder> A('a');
  Constraint<placeholder> B('b');
  Constraint<placeholder> C('c');
  Constraint<placeholder> D('d');

  std::atomic<uint64_t> count{ 0 };

  Rule barrier({ A, B, C, D }, [&](std::vector<Constraint<uint64_t>*> cs) {
    ++count;
  });

  worker.add_rule(&barrier);

  omp_set_num_threads(4);

  int64_t big_n;

  auto start = std::chrono::high_resolution_clock::now();

#pragma omp parallel for 
  for (int i = 0; i < 4000000; ++i)
  {
    int n = 0;
    for (size_t j = 0; j < 1000; ++j)
      n += rand() % 10;

    big_n += n;

    auto num = omp_get_thread_num();
    
    switch (num) {
    case 0:
      worker.sync(A, { 42 }, m);
      break;
    case 1:
      worker.sync(B, { 42 }, m);
      break;
    case 2:
      worker.sync(C, { 42 }, m);
      break;
    default:
      worker.sync(D, { 42 }, m);
    }
    
  }

  worker.wait();

  auto end = std::chrono::high_resolution_clock::now();
  std::cout << count.load() << "\n";
  output << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

  return true;

}