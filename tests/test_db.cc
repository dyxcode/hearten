#include <iostream>
#include <string>
#include <random>
#include <unistd.h>
#include <algorithm>
#include <thread>
#include <map>
#include <chrono>

#include "bitcask/bitcaskdb.h"

std::string random_string( size_t length ) {
  auto randchar = []() -> char {
    const char charset[] = "0123456789"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[ rand() % max_index ];
  };
  std::string str(length,0);
  std::generate_n( str.begin(), length, randchar );
  return str;
}

void testSpeed() {
  using namespace std::chrono;
  std::map<std::string, std::string> kvs;
  for (int32_t i = 0; i < 1024 * 1024; i++) {
    std::string key = random_string(8);
    std::string value = random_string(1024);
    kvs[key] = value;
  }

  hearten::BitcaskDB db{"test_speed", std::filesystem::current_path()};

  auto t_begin = steady_clock::now();
  for (auto&& [key, value] : kvs) {
    db.put(key, value);
  }
  auto t_end = steady_clock::now();
  auto ms = duration_cast<milliseconds>(t_end - t_begin);
  DEBUG << "put 1G, spend " << ms.count() << "ms";

  t_begin = steady_clock::now();
  for (auto&& [key, value] : kvs) {
    auto read_value = db.get(key);
    ASSERT(read_value && *read_value == value);
  }
  t_end = steady_clock::now();
  ms = duration_cast<milliseconds>(t_end - t_begin);
  DEBUG << "get 1G, spend " << ms.count() << "ms";

  t_begin = steady_clock::now();
  for (auto&& [key, value] : kvs) {
    db.remove(key);
  }
  t_end = steady_clock::now();
  ms = duration_cast<milliseconds>(t_end - t_begin);
  DEBUG << "delete 1G, spend " << ms.count() << "ms";
}

int main() {
  std::map<std::string, std::string> kvs;
  for (int32_t i = 0; i < 1024; i++) {
    std::string key = random_string(8);
    std::string value = random_string(4096);
    kvs[key] = value;
  }
  {
    hearten::BitcaskDB db{"test", std::filesystem::current_path()};
    for (auto&& [key, value] : kvs) {
      db.put(key, value);
      auto read_value = db.get(key);
      ASSERT(read_value && *read_value == value);
    }
  }

  hearten::BitcaskDB db{"test", std::filesystem::current_path()};
  for (auto&& [key, value] : kvs) {
    auto read_value = db.get(key);
    ASSERT(read_value && *read_value == value);
  }

  // Delete
  for (auto&& [key, value] : kvs) {
    db.remove(key);
  }

  for (auto&& [key, value] : kvs) {
    ASSERT(!db.get(key));
  }
  testSpeed();
  return 0;
}

