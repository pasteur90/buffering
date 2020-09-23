#include <iostream>
#include <memory>
#include <vector>

#include "buffering.hpp"

int main() {
  TBuffer<int, 3> tb{1, 2, 3};
  {
    auto p = tb.select();
  }

  {
    auto p2 = tb.acquire();
    std::cout << *p2 << std::endl;
  }

  return 0;
}
