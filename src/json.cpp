#include "quickapp/android/json.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace quickapp::android {
namespace {

RuntimeError invalid(std::string message) {
  return error(ErrorCode::kAbiInvalidArgument, std::move(message));
}

class Parser {
 public:
  explicit Parser(std::string_view source) : source_(source) {}

  Result<JsonValue> parse() {
    skipWhitespace();
    auto value = parseValue(0);
    if (!value.ok()) {
      return value;
    }
    skipWhitespace();
    if (position_ != source_.size()) {
      return Result<JsonValue>::failure(invalid("trailing JSON content"));
    }
    return value;
  }

 private:
  static constexpr std::size_t kMaxDepth = 128;

  void skipWhitespace() {
    while (position_ < source_.size()) {
      const char c = source_[position_];
      if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
        return;
      }
      ++position_;
    }
  }

  Result<JsonValue> parseValue(std::size_t depth) {
    if (depth > kMaxDepth || position_ >= source_.size()) {
      return Result<JsonValue>::failure(invalid("invalid JSON value"));
    }
    switch (source_[position_]) {
      case 'n':
        return parseLiteral("null", JsonValue::Storage{std::monostate{}});
      case 't':
        return parseLiteral("true", JsonValue::Storage{true});
      case 'f':
        return parseLiteral("false", JsonValue::Storage{false});
      case '"': {
        auto value = parseString();
        if (!value.ok()) {
          return Result<JsonValue>::failure(value.error());
        }
        return Result<JsonValue>::success(
            JsonValue(JsonValue::Storage{std::move(value.value())}));
      }
      case '[':
        return parseArray(depth + 1);
      case '{':
        return parseObject(depth + 1);
      default:
        return parseNumber();
    }
  }

  Result<JsonValue> parseLiteral(std::string_view literal,
                                 JsonValue::Storage value) {
    if (source_.substr(position_, literal.size()) != literal) {
      return Result<JsonValue>::failure(invalid("invalid JSON literal"));
    }
    position_ += literal.size();
    return Result<JsonValue>::success(JsonValue(std::move(value)));
  }

  static void appendUtf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
      output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  Result<std::string> parseString() {
    if (source_[position_++] != '"') {
      return Result<std::string>::failure(invalid("expected string"));
    }
    std::string output;
    while (position_ < source_.size()) {
      const unsigned char c = source_[position_++];
      if (c == '"') {
        return Result<std::string>::success(std::move(output));
      }
      if (c < 0x20) {
        return Result<std::string>::failure(
            invalid("control character in string"));
      }
      if (c != '\\') {
        output.push_back(static_cast<char>(c));
        continue;
      }
      if (position_ >= source_.size()) {
        return Result<std::string>::failure(invalid("invalid string escape"));
      }
      const char escaped = source_[position_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          output.push_back(escaped);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        case 'u': {
          if (position_ + 4 > source_.size()) {
            return Result<std::string>::failure(
                invalid("truncated unicode escape"));
          }
          std::uint32_t codepoint = 0;
          for (int i = 0; i < 4; ++i) {
            const char digit = source_[position_++];
            codepoint <<= 4;
            if (digit >= '0' && digit <= '9') {
              codepoint += static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
              codepoint += static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
              codepoint += static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
              return Result<std::string>::failure(
                  invalid("invalid unicode escape"));
            }
          }
          if (codepoint >= 0xd800 && codepoint <= 0xdfff) {
            return Result<std::string>::failure(
                invalid("surrogate escapes are not accepted"));
          }
          appendUtf8(output, codepoint);
          break;
        }
        default:
          return Result<std::string>::failure(invalid("invalid string escape"));
      }
    }
    return Result<std::string>::failure(invalid("unterminated string"));
  }

  Result<JsonValue> parseArray(std::size_t depth) {
    ++position_;
    skipWhitespace();
    JsonValue::Array values;
    if (position_ < source_.size() && source_[position_] == ']') {
      ++position_;
      return Result<JsonValue>::success(
          JsonValue(JsonValue::Storage{std::move(values)}));
    }
    while (true) {
      auto value = parseValue(depth);
      if (!value.ok()) {
        return value;
      }
      values.push_back(std::move(value.value()));
      skipWhitespace();
      if (position_ >= source_.size()) {
        return Result<JsonValue>::failure(invalid("unterminated array"));
      }
      const char delimiter = source_[position_++];
      if (delimiter == ']') {
        break;
      }
      if (delimiter != ',') {
        return Result<JsonValue>::failure(invalid("invalid array delimiter"));
      }
      skipWhitespace();
    }
    return Result<JsonValue>::success(
        JsonValue(JsonValue::Storage{std::move(values)}));
  }

  Result<JsonValue> parseObject(std::size_t depth) {
    ++position_;
    skipWhitespace();
    JsonValue::Object values;
    if (position_ < source_.size() && source_[position_] == '}') {
      ++position_;
      return Result<JsonValue>::success(
          JsonValue(JsonValue::Storage{std::move(values)}));
    }
    while (true) {
      if (position_ >= source_.size() || source_[position_] != '"') {
        return Result<JsonValue>::failure(invalid("object key must be string"));
      }
      auto key = parseString();
      if (!key.ok()) {
        return Result<JsonValue>::failure(key.error());
      }
      skipWhitespace();
      if (position_ >= source_.size() || source_[position_++] != ':') {
        return Result<JsonValue>::failure(invalid("missing object colon"));
      }
      skipWhitespace();
      auto value = parseValue(depth);
      if (!value.ok()) {
        return value;
      }
      const auto [unused, inserted] =
          values.emplace(std::move(key.value()), std::move(value.value()));
      (void)unused;
      if (!inserted) {
        return Result<JsonValue>::failure(invalid("duplicate object key"));
      }
      skipWhitespace();
      if (position_ >= source_.size()) {
        return Result<JsonValue>::failure(invalid("unterminated object"));
      }
      const char delimiter = source_[position_++];
      if (delimiter == '}') {
        break;
      }
      if (delimiter != ',') {
        return Result<JsonValue>::failure(invalid("invalid object delimiter"));
      }
      skipWhitespace();
    }
    return Result<JsonValue>::success(
        JsonValue(JsonValue::Storage{std::move(values)}));
  }

  Result<JsonValue> parseNumber() {
    const std::size_t start = position_;
    if (position_ < source_.size() && source_[position_] == '-') {
      ++position_;
    }
    if (position_ >= source_.size()) {
      return Result<JsonValue>::failure(invalid("invalid number"));
    }
    if (source_[position_] == '0') {
      ++position_;
      if (position_ < source_.size() && source_[position_] >= '0' &&
          source_[position_] <= '9') {
        return Result<JsonValue>::failure(invalid("leading zero in number"));
      }
    } else {
      if (source_[position_] < '1' || source_[position_] > '9') {
        return Result<JsonValue>::failure(invalid("invalid number"));
      }
      while (position_ < source_.size() && source_[position_] >= '0' &&
             source_[position_] <= '9') {
        ++position_;
      }
    }
    bool integer = true;
    if (position_ < source_.size() && source_[position_] == '.') {
      integer = false;
      ++position_;
      const std::size_t fraction = position_;
      while (position_ < source_.size() && source_[position_] >= '0' &&
             source_[position_] <= '9') {
        ++position_;
      }
      if (fraction == position_) {
        return Result<JsonValue>::failure(invalid("invalid fraction"));
      }
    }
    if (position_ < source_.size() &&
        (source_[position_] == 'e' || source_[position_] == 'E')) {
      integer = false;
      ++position_;
      if (position_ < source_.size() &&
          (source_[position_] == '+' || source_[position_] == '-')) {
        ++position_;
      }
      const std::size_t exponent = position_;
      while (position_ < source_.size() && source_[position_] >= '0' &&
             source_[position_] <= '9') {
        ++position_;
      }
      if (exponent == position_) {
        return Result<JsonValue>::failure(invalid("invalid exponent"));
      }
    }
    const std::string number(source_.substr(start, position_ - start));
    char* end = nullptr;
    const double parsed = std::strtod(number.c_str(), &end);
    if (end != number.c_str() + number.size() || !std::isfinite(parsed)) {
      return Result<JsonValue>::failure(invalid("non-finite number"));
    }
    if (integer && std::abs(parsed) > 9007199254740991.0) {
      return Result<JsonValue>::failure(
          invalid("integer exceeds JavaScript safe range"));
    }
    return Result<JsonValue>::success(
        JsonValue(JsonValue::Storage{JsonNumber{parsed, integer}}));
  }

  std::string_view source_;
  std::size_t position_ = 0;
};

}  // namespace

bool JsonValue::isNull() const noexcept {
  return std::holds_alternative<std::monostate>(storage_);
}
const bool* JsonValue::asBool() const noexcept {
  return std::get_if<bool>(&storage_);
}
const JsonNumber* JsonValue::asNumber() const noexcept {
  return std::get_if<JsonNumber>(&storage_);
}
const std::string* JsonValue::asString() const noexcept {
  return std::get_if<std::string>(&storage_);
}
const JsonValue::Array* JsonValue::asArray() const noexcept {
  return std::get_if<Array>(&storage_);
}
const JsonValue::Object* JsonValue::asObject() const noexcept {
  return std::get_if<Object>(&storage_);
}

Result<JsonValue> parseJson(std::string_view source) {
  return Parser(source).parse();
}

}  // namespace quickapp::android
