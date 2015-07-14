#pragma once
#include <string>
#include <tbb/concurrent_hash_map.h>
#include <atomic>
#include <fstream>
#include <stdint.h>

class Statistics
{
public:
  uint64_t TIME;
  std::atomic<uint32_t> ALREADYCONSUMED;
  std::atomic<uint32_t> NOTFOUND;
  std::atomic<uint32_t> NOMATCH;
  std::atomic<uint32_t> UNCOMPLETELIST;
  std::atomic<uint32_t> CLAIMFAILED;
  std::atomic<uint32_t> TIMEOUT;

  static Statistics& instance()
  {
    static Statistics stats;
    return stats;
  }

  void reset()
  {
    TIME = 0;
    ALREADYCONSUMED.store(0);
    NOTFOUND.store(0);
    NOMATCH.store(0);
    UNCOMPLETELIST.store(0);
    CLAIMFAILED.store(0);
    TIMEOUT.store(0);
  }

  void write(std::string filename)
  {
    std::ofstream file(filename);
    file << "time: " << TIME << std::endl;
    file << "already consumed: " << ALREADYCONSUMED.load() << std::endl;
    file << "not found: " << NOTFOUND.load() << std::endl;
    file << "no match: " << NOMATCH.load() << std::endl;
    file << "uncomplete list: " << UNCOMPLETELIST.load() << std::endl;
    file << "claim failed: " << CLAIMFAILED.load() << std::endl;
    file << "timeout: " << TIMEOUT.load() << std::endl;

    file.close();
  }

private:
  Statistics() : TIME{ 0 }, ALREADYCONSUMED{ 0 }, NOTFOUND{ 0 }, NOMATCH{ 0 }, UNCOMPLETELIST{ 0 }, CLAIMFAILED{ 0 }, TIMEOUT{ 0 }
  {
  }
};