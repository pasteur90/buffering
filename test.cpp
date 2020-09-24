#include <iostream>
#include <memory>
#include <vector>
#include <thread>

#include "buffering.hpp"

void reader(decltype(TBuffer<int, 3>::make()) tb) {
  while (true) {
    auto p = tb->acquire();
    std::cout << *p << std::endl;
  }
}

void writer(decltype(TBuffer<int, 3>::make()) tb) {
  for (int i = 0; i < 100000;) {
    auto p = tb->select();
    if (p != nullptr) {
      *p = i;
      ++i;
    }
  }
}

int main() {
  auto tb = TBuffer<int, 3>::make();

  auto read_th1 = std::thread(reader, tb);
  auto read_th2 = std::thread(reader, tb);

  auto write_th1 = std::thread(writer, tb);
  auto write_th2 = std::thread(writer, tb);

  read_th1.join();
  read_th2.join();

  write_th1.join();
  write_th2.join();

  return 0;
}
