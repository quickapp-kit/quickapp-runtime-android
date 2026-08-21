#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "quickapp/android/result.h"

namespace quickapp::android {

struct JsonNumber {
  double value;
  bool integer;
};

class JsonValue {
 public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue, std::less<>>;
  using Storage =
      std::variant<std::monostate, bool, JsonNumber, std::string, Array, Object>;

  explicit JsonValue(Storage storage) : storage_(std::move(storage)) {}

  bool isNull() const noexcept;
  const bool* asBool() const noexcept;
  const JsonNumber* asNumber() const noexcept;
  const std::string* asString() const noexcept;
  const Array* asArray() const noexcept;
  const Object* asObject() const noexcept;

 private:
  Storage storage_;
};

Result<JsonValue> parseJson(std::string_view source);

}  // namespace quickapp::android

