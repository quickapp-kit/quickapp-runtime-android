#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace quickapp::android {

enum class ErrorCode {
  kAbiInvalidArgument,
  kAbiUnsupportedVersion,
  kLifecycleBusy,
  kPackageNotFound,
  kPackageIoError,
  kModuleAbiUnsupported,
  kRuntimeProfileIncompatible,
  kPlatformRejected,
};

std::string_view errorCodeName(ErrorCode code) noexcept;

struct RuntimeError {
  ErrorCode code;
  std::string message;
  bool retryable = false;

  friend bool operator==(const RuntimeError&, const RuntimeError&) = default;
};

template <typename T>
class Result {
 public:
  static Result success(T value) { return Result(std::move(value)); }
  static Result failure(RuntimeError error) { return Result(std::move(error)); }

  bool ok() const noexcept { return std::holds_alternative<T>(value_); }
  const T& value() const { return std::get<T>(value_); }
  T& value() { return std::get<T>(value_); }
  const RuntimeError& error() const { return std::get<RuntimeError>(value_); }

 private:
  explicit Result(T value) : value_(std::move(value)) {}
  explicit Result(RuntimeError error) : value_(std::move(error)) {}
  std::variant<T, RuntimeError> value_;
};

struct Unit {
  friend bool operator==(Unit, Unit) = default;
};

using Status = Result<Unit>;

inline RuntimeError error(ErrorCode code, std::string message,
                          bool retryable = false) {
  return RuntimeError{code, std::move(message), retryable};
}

}  // namespace quickapp::android
