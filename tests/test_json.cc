#include "json/json.h"

int main() {
  hearten::Json json;
  json.parse(R"(true)");
  DEBUG << json.toString();
  json.parse(R"(false)");
  DEBUG << json.toString();
  json.parse(R"(null)");
  DEBUG << json.toString();
  json.parse(R"(123)");
  DEBUG << json.toString();
  json.parse(R"("string")");
  DEBUG << json.toString();
  json.parse(R"([1,2,3,4,5])");
  DEBUG << json.toString();
  json.parse(R"({"key": ["value1", "value2"]})");
  DEBUG << json.toString();

  json.parse(R"({"title": "Design Patterns",
    "author": "John Vlissides"})");
  DEBUG << json.toString();

  json.parse(R"({"title": "Design Patterns",
    "subtitle": "Elements of Reusable Object-Oriented Software",
    "author": [
        "Erich Gamma",
        "Richard Helm",
        "Ralph Johnson",
        "John Vlissides"
    ],
    "year": 2009,
    "weight": 1.8,
    "hardcover": true,
    "publisher": {
        "Company": "Pearson Education",
        "Country": "India"
    },
    "website": null})");
  DEBUG << json.toString();

  return 0;
}
