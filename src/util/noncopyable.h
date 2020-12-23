#ifndef HEARTEN_UTIL_NONCOPYABLE_H_
#define HEARTEN_UTIL_NONCOPYABLE_H_

namespace hearten {

class Noncopyable {
public:
  Noncopyable(const Noncopyable&) = delete;
  void operator=(const Noncopyable&) = delete;

protected:
  Noncopyable() = default;
  ~Noncopyable() = default;
  Noncopyable(Noncopyable&&) = default;
  Noncopyable& operator=(Noncopyable&&) = default;
};

} // namespace hearten

#endif
