#ifndef HEARTEN_JSON_H_
#define HEARTEN_JSON_H_

#include <vector>
#include <string>
#include <unordered_map>
#include <variant>

#include "log.h"

namespace hearten {

namespace detail {

inline std::string_view skipSpace(std::string_view src) {
  src.remove_prefix(std::min(src.find_first_not_of(" \t\n"), src.size()));
  return src;
}

struct JsonNode {
  using Null = std::monostate;
  using Boolean = bool;
  using Number = double;
  using String = std::string;
  using Array = std::vector<std::unique_ptr<JsonNode>>;
  using Object = std::unordered_map<std::string, std::unique_ptr<JsonNode>>;

  std::string_view setValid() { data_ = std::monostate(); return ""; }
  std::string_view parse(std::string_view src) {
    src = skipSpace(src);
    switch (src[0]) {
      case 't':  return parse<true>(src);
      case 'f':  return parse<false>(src);
      case 'n':  return parse<Null>(src);
      default:   return parse<Number>(src);
      case '"':  return parse<String>(src);
      case '[':  return parse<Array>(src);
      case '{':  return parse<Object>(src);
    }
  }
  template<bool truth> std::string_view parse(std::string_view src) {
    if constexpr (truth == true) {
      if (src.size() < 4 || src.substr(0, 4) != "true") return setValid();
      else { data_ = true; return src.substr(4); }
    } else {
      if (src.size() < 5 || src.substr(0, 5) != "false") return setValid();
      else { data_ = false; return src.substr(5); }
    }
  }
  template<typename> std::string_view parse(std::string_view src) {
    if (src.size() < 4 || src.substr(0, 4) != "null") return setValid();
    else { data_ = std::monostate(); return src.substr(4); }
  }
  template<> std::string_view parse<Number>(std::string_view src) {
    auto isDigit = [](char ch) { return (ch >= '0' && ch <= '9'); };
    auto isPositive = [](char ch) { return (ch >= '1' && ch <= '9'); };
    auto isSign = [](char ch) { return (ch == '+' || ch == '-'); };
    auto isIndex = [](char ch) { return (ch == 'e' || ch == 'E'); };

    size_t i = 0;
    if (src[i] == '-' && ++i == src.size()) return setValid();
    if (src[i] != '0') {
      if (!(isPositive(src[i]))) return setValid();
      for (++i; i != src.size() && isDigit(src[i]); ++i);
    } else ++i;
    if (i != src.size() && src[i] == '.') {
      if (++i == src.size() || !isDigit(src[i])) return setValid();
      for (++i; i != src.size() && isDigit(src[i]); ++i);
    }
    if (i != src.size() && isIndex(src[i])) {
      if (++i == src.size()) return setValid();
      if (isSign(src[i]) && ++i == src.size()) return setValid();
      if (!isDigit(src[i])) return setValid();
      for (++i; i != src.size() && isDigit(src[i]); ++i);
    }
    size_t non_number_pos;
    data_ = std::stod(std::string(src), &non_number_pos);
    ASSERT(non_number_pos == i);
    return src.substr(non_number_pos);
  }
  template<> std::string_view parse<String>(std::string_view src) {
    String ret;
    for (size_t i = 1; i != src.size(); ++i) {
      if (src[i] != '\"') ret.push_back(src[i]);
      else {
        data_ = std::move(ret);
        return src.substr(i + 1);
      }
    }
    return setValid();
  }
  template<> std::string_view parse<Array>(std::string_view src) {
    Array ret;
    src = skipSpace(src.substr(1));
    while (!src.empty()) {
      if (src[0] == ',') src = skipSpace(src.substr(1));
      else if (src[0] == ']') {
        data_ = std::move(ret);
        return src.substr(1);
      } else {
        auto node = std::make_unique<JsonNode>();
        src = skipSpace(node->parse(src));
        ret.push_back(std::move(node));
      }
    }
    return setValid();
  }
  template<> std::string_view parse<Object>(std::string_view src) {
    Object ret;
    src = skipSpace(src.substr(1));
    while (!src.empty()) {
      if (src[0] == ',') src = skipSpace(src.substr(1));
      else if (src[0] == '}') {
        data_ = std::move(ret);
        return src.substr(1);
      } else {
        ASSERT(src[0] == '"');
        JsonNode key; // tranfrom into string
        src = skipSpace(key.parse<String>(src));
        if (src.empty() || src[0] != ':') return setValid();
        src = skipSpace(src.substr(1));
        auto value = std::make_unique<JsonNode>();
        src = skipSpace(value->parse(src));
        ret.emplace(std::get<String>(std::move(key.data_)), std::move(value));
      }
    }
    return setValid();
  }

  template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
  template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

  std::string stringify() const {
    std::ostringstream os;
    std::visit(overloaded{
      [&os](Null) { os << "null"; },
      [&os](Boolean arg) {
        if (arg) os << "true";
        else os << "false";
      },
      [&os](Number arg) { os << arg; },
      [&os](const String& arg) { os << '\"' << arg << '\"'; },
      [&os](const Array& arg) {
        os << '[';
        for (size_t i = 0; i != arg.size(); ++i) {
          os << arg[i]->stringify();
          if (i != arg.size() - 1) os << ',';
        }
        os << ']';
      },
      [&os](const Object& arg) {
        os << '{';
        for (auto it = arg.cbegin(); it != arg.cend(); ++it) {
          auto&& [key, value] = *it;
          os << '\"' << key << '\"' << ':' << value->stringify();
          if (std::next(it) != arg.cend()) os << ',';
        }
        os << '}';
      }
    }, data_);
    return os.str();
  }

  using NodeType = std::variant<Null, Boolean, Number, String, Array, Object>;
  NodeType data_;
};

} // namespace detail

class Json {
public:
  Json() = default;
  Json(std::string_view src) {
    parse(src);
  }
  void parse(std::string_view src) {
    if (!src.empty()) node_.parse(src);
  }
  std::string toString() const { return node_.stringify(); }

  operator detail::JsonNode::NodeType&() { return node_.data_; }

private:
  detail::JsonNode node_;
};

} // namespace hearten

#endif
