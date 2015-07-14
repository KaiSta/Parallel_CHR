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

#include "test_mpi.h"

#ifdef _WIN32
#include <Windows.h>
#endif

static std::ofstream logfile;


#define PERF

int main(int argc, char** argv)
{

  printf("PESSIMISTIC\n");
  test_barrier(std::cout, PESSIMISTIC_RESTART);
  test_barrier(std::cout, PESSIMISTIC_RESTART);
  test_barrier(std::cout, PESSIMISTIC_RESTART);
  printf("OPTC\n");
  test_barrier(std::cout, OPTIMISTIC_RESTART);
  test_barrier(std::cout, OPTIMISTIC_RESTART);
  test_barrier(std::cout, OPTIMISTIC_RESTART);
  printf("OPTpartial\n");
  test_barrier(std::cout, OPTIMISTIC_PARTIAL_RESTART);
  test_barrier(std::cout, OPTIMISTIC_PARTIAL_RESTART);
  test_barrier(std::cout, OPTIMISTIC_PARTIAL_RESTART);
  system("pause");
  return 0;

  {
    for (int core = 1; core <= 8; core *= 2)
    {
      auto cores = core;
      std::stringstream ss;
      ss << "feistelmix1 " << cores << "(" << std::pow(2, cores) - 1 << ")" << "cores.txt";
      std::ofstream file(ss.str());

      DWORD_PTR mask = static_cast<int>(std::pow(2, cores)) - 1;
      auto succ = SetProcessAffinityMask(GetCurrentProcess(), mask);

      file << "dining philosophers:\n";
      test_dining_philosophers(file, OPTIMISTIC_PARTIAL_RESTART);
      file << "blocksworld:\n";
      test_blocksworld(file, PESSIMISTIC_RESTART);
      file << "unordered map:\n";
      test_unordered_map(file, PESSIMISTIC_RESTART);
      file << "producer consumer:\n";
      producer_consumer(file, PESSIMISTIC_RESTART);
      file << "mutex:\n";
      test_mutex(file, PESSIMISTIC_RESTART);
      file << "acc:\n";
      test_sum(file, PESSIMISTIC_RESTART);
      file << "feistel:\n";
      test_feistel_cipher(file, PESSIMISTIC_RESTART);
      file.close();
    }
    system("pause");
    return 0;
  }
}
