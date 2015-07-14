#pragma once
struct placeholder
{
  placeholder(){}
  placeholder(char x) : c(x)
  {}
  char c;

  operator char() const
  {
    return c;
  }
};