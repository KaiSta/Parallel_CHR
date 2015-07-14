#include "ID_Manager.h"


ID_Manager::ID_Manager()
{
}


ID_Manager::~ID_Manager()
{
}

uint32_t ID_Manager::get()
{
  static std::atomic<uint32_t> id{0};

  return id++;
}
