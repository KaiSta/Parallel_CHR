#include <stdint.h>
#pragma once

static uint32_t murmurhash3_32bit(uint32_t i)
{
  i ^= i >> 16;
  i *= 0x85ebca6b;
  i ^= i >> 13;
  i *= 0xc2b2ae35;
  i ^= i >> 16;
  return i;
}

static uint64_t murmurhash3_64bit(uint64_t i)
{
  i ^= i >> 33;
  i *= 0xff51afd7ed558ccd;
  i ^= i >> 33;
  i *= 0xc4ceb9fe1a85ec53;
  i ^= i >> 33;
  return i;
}