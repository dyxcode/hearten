#ifndef HEARTEN_UTIL_RANDOM_H_
#define HEARTEN_UTIL_RANDOM_H_

#include <random>

namespace hearten {

class Random {
public:
  static uint32_t get_uint32() {
    std::random_device r;
    std::default_random_engine e{r()};
    std::uniform_int_distribution<uint32_t> dist{0};
    return dist(e);
  }
};

} // namespace hearten

#endif
