#include "quickapp/android/result.h"

#include <string_view>

namespace quickapp::android {

std::string_view errorCodeName(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kAbiInvalidArgument:
      return "ABI_INVALID_ARGUMENT";
    case ErrorCode::kAbiUnsupportedVersion:
      return "ABI_UNSUPPORTED_VERSION";
    case ErrorCode::kLifecycleBusy:
      return "LIFECYCLE_BUSY";
    case ErrorCode::kPackageNotFound:
      return "PACKAGE_NOT_FOUND";
    case ErrorCode::kPackageIoError:
      return "PACKAGE_IO_ERROR";
    case ErrorCode::kModuleAbiUnsupported:
      return "MODULE_ABI_UNSUPPORTED";
    case ErrorCode::kRuntimeProfileIncompatible:
      return "RUNTIME_PROFILE_INCOMPATIBLE";
    case ErrorCode::kPlatformRejected:
      return "PLATFORM_REJECTED";
  }
  return "PLATFORM_REJECTED";
}

}  // namespace quickapp::android

