#include <atomic>
#include <stdint.h>


#pragma once
class ID_Manager
{
public:
  ID_Manager();
  ~ID_Manager();
  static uint32_t get();
};

