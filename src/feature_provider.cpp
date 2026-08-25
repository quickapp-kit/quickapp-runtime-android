#include "quickapp/android/feature_provider.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace quickapp::android::platform {
namespace {

namespace qcf = core::feature;

qcf::Result base(const qcf::Request& request, qcf::Status status) {
  return {request.request_id, request.surface_id, status, std::nullopt,
          std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
          std::nullopt, std::nullopt};
}

bool contains(std::string_view value, std::string_view needle) noexcept {
  return value.find(needle) != std::string_view::npos;
}

}  // namespace

AndroidFeatureProvider::AndroidFeatureProvider() noexcept {
  files_.emplace("private/platform-state.txt", "android-provider-ready");
}

qcf::Result AndroidFeatureProvider::failure(
    const qcf::Request& request, const char* code,
    const char* message) const noexcept {
  auto result = base(request, qcf::Status::kFailed);
  result.error = qcf::Error{code, message, false};
  return result;
}

qcf::Result AndroidFeatureProvider::unsupported(
    const qcf::Request& request, const char* message) const noexcept {
  auto result = base(request, qcf::Status::kUnsupported);
  result.error = qcf::Error{"CAPABILITY_UNSUPPORTED", message, false};
  return result;
}

bool AndroidFeatureProvider::validPrivatePath(const std::string& path) const noexcept {
  return path.starts_with("private/") && !path.empty() &&
         !contains(path, "..") && !contains(path, "//");
}

qcf::Result AndroidFeatureProvider::invoke(
    const qcf::Request& request) noexcept {
  try {
    if (request.module == qcf::ModuleId::kSystemPrompt) {
      if (request.method == qcf::Method::kShowToast) {
        if (request.text.empty()) return failure(request, "INVALID_ARGUMENT",
                                                 "prompt text is empty");
        return base(request, qcf::Status::kSuccess);
      }
      if (request.method == qcf::Method::kAlert ||
          request.method == qcf::Method::kConfirm) {
        if (request.text.empty()) return failure(request, "INVALID_ARGUMENT",
                                                 "prompt text is empty");
        if (contains(request.text, "cancel")) {
          return base(request, qcf::Status::kCancelled);
        }
        auto result = base(request, qcf::Status::kSuccess);
        if (request.method == qcf::Method::kConfirm) result.confirmed = true;
        return result;
      }
      return unsupported(request, "Android prompt method is unavailable");
    }

    if (request.module == qcf::ModuleId::kSystemFetch) {
      if (request.method == qcf::Method::kFetchCancel) {
        return unsupported(request, "fetch cancellation requires an in-flight request");
      }
      if (request.method != qcf::Method::kFetch || !request.url ||
          request.url->empty()) {
        return failure(request, "INVALID_ARGUMENT", "fetch URL is required");
      }
      if (*request.url == "local://platform/status") {
        auto result = base(request, qcf::Status::kSuccess);
        result.http_status = 200;
        if (request.response_type == "json") {
          result.response_body =
              "{\"status\":\"ok\",\"provider\":\"android-deterministic\"}";
          result.response_is_json = true;
        } else {
          result.response_body = "status=ok;provider=android-deterministic";
          result.response_is_json = false;
        }
        return result;
      }
      if (*request.url == "local://platform/failure") {
        return failure(request, "FETCH_DETERMINISTIC_FAILURE",
                       "deterministic local fetch failed");
      }
      if (*request.url == "local://platform/cancelled") {
        cancelled_requests_.insert(request.surface_id.wire() + "\n" +
                                   request.request_id.wire());
        return base(request, qcf::Status::kCancelled);
      }
      return unsupported(request, "Android deterministic fetch URL is unavailable");
    }

    if (request.module == qcf::ModuleId::kSystemFile) {
      if (!request.path || !validPrivatePath(*request.path)) {
        return failure(request, "FILE_PATH_REJECTED",
                       "file path must stay under private/");
      }
      const auto& path = *request.path;
      if (request.method == qcf::Method::kFileRead) {
        const auto found = files_.find(path);
        if (found == files_.end()) {
          return failure(request, "FILE_NOT_FOUND", "private file does not exist");
        }
        auto result = base(request, qcf::Status::kSuccess);
        result.file_data = found->second;
        return result;
      }
      if (request.method == qcf::Method::kFileWrite) {
        if (!request.data) return failure(request, "INVALID_ARGUMENT",
                                           "file data is required");
        files_[path] = *request.data;
        return base(request, qcf::Status::kSuccess);
      }
      if (request.method == qcf::Method::kFileExists) {
        auto result = base(request, qcf::Status::kSuccess);
        result.file_exists = files_.contains(path);
        return result;
      }
      if (request.method == qcf::Method::kFileDelete) {
        files_.erase(path);
        return base(request, qcf::Status::kSuccess);
      }
      return unsupported(request, "Android file method is unavailable");
    }

    return unsupported(request, "Android feature module is unavailable");
  } catch (...) {
    return failure(request, "CAPABILITY_FAILED", "Android feature provider failed");
  }
}

bool AndroidFeatureProvider::cancel(const core::RequestId& request_id,
                                    const core::SurfaceId& surface_id) noexcept {
  const auto key = surface_id.wire() + "\n" + request_id.wire();
  return cancelled_requests_.erase(key) != 0;
}

void AndroidFeatureProvider::teardown(const core::SurfaceId& surface_id) noexcept {
  const auto prefix = surface_id.wire() + "\n";
  for (auto it = cancelled_requests_.begin(); it != cancelled_requests_.end();) {
    if (it->starts_with(prefix)) it = cancelled_requests_.erase(it);
    else ++it;
  }
}

}  // namespace quickapp::android::platform
