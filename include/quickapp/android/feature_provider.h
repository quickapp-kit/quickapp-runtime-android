#pragma once

#include <map>
#include <set>
#include <string>

#include "quickapp/core/feature/module_registry.h"

namespace quickapp::android::platform {

class AndroidFeatureProvider final : public core::feature::Provider {
 public:
  AndroidFeatureProvider() noexcept;

  [[nodiscard]] core::feature::Result invoke(
      const core::feature::Request& request) noexcept override;
  [[nodiscard]] bool cancel(const core::RequestId& request_id,
                            const core::SurfaceId& surface_id) noexcept override;
  void teardown(const core::SurfaceId& surface_id) noexcept override;

 private:
  [[nodiscard]] core::feature::Result failure(
      const core::feature::Request& request, const char* code,
      const char* message) const noexcept;
  [[nodiscard]] core::feature::Result unsupported(
      const core::feature::Request& request, const char* message) const noexcept;
  [[nodiscard]] bool validPrivatePath(const std::string& path) const noexcept;

  std::map<std::string, std::string, std::less<>> files_;
  std::set<std::string, std::less<>> cancelled_requests_;
};

}  // namespace quickapp::android::platform
