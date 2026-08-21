#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "quickapp/android/contracts.h"
#include "quickapp/android/package_source.h"

namespace quickapp::android {

class TraceSink {
 public:
  virtual ~TraceSink() = default;
};

class NoopTraceSink final : public TraceSink {};

class JsEngineProvider {
 public:
  virtual ~JsEngineProvider() = default;
  virtual const JsEngineIdentity& identity() const noexcept = 0;
};

Result<RuntimeCompositionManifest> decodeRuntimeCompositionManifest(
    std::string_view json);

struct CompositionSelection {
  std::vector<std::shared_ptr<JsEngineProvider>> engineProviders;
  std::shared_ptr<TraceSink> noopTraceSink;
  std::shared_ptr<TraceSink> recordingTraceSink;
  std::vector<std::string> linkedModuleIds;
  std::uint64_t binaryBytes;
};

struct ComposedRuntime {
  RuntimeCompositionManifest manifest;
  std::shared_ptr<JsEngineProvider> engineProvider;
  std::shared_ptr<TraceSink> traceSink;
};

class AndroidCompositionRoot {
 public:
  static Result<ComposedRuntime> compose(
      RuntimeCompositionManifest manifest,
      const CompositionSelection& selection);
};

}  // namespace quickapp::android
