#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "quickapp/android/json.h"

namespace quickapp::android {

struct Viewport {
  double width;
  double height;
};

struct RuntimeLaunchProfile {
  std::string artifact;
  std::string entryRoute;
  JsonValue::Object params;
  Viewport viewport;
  std::string traceOutput;
};

enum class Conformance { kV1, kCustom };
enum class BuildMode { kDebug, kRelease };
enum class ObservationLevel { kOff, kBaseline, kDiagnostic };

struct JsEngineIdentity {
  std::string engineId;
  std::string engineVersion;
  std::string engineAbi;
  std::string moduleId;

  friend bool operator==(const JsEngineIdentity&,
                         const JsEngineIdentity&) = default;
};

struct LinkedModule {
  std::string moduleId;
  std::string category;
  std::string version;
};

struct RuntimeCompositionManifest {
  std::string profileId;
  std::string runtimeAbi;
  Conformance conformance;
  BuildMode buildMode;
  ObservationLevel observationLevel;
  JsEngineIdentity jsEngine;
  std::vector<LinkedModule> linkedModules;
  std::vector<std::string> components;
  std::vector<std::string> capabilities;
  std::uint64_t binaryBytes;
};

enum class LifecycleAction {
  kEnterForeground,
  kEnterBackground,
  kDestroyAppRuntime,
};

enum class RuntimeState { kForeground, kBackground, kDestroyed };

struct RuntimeLifecycleControl {
  std::string requestId;
  LifecycleAction action;
};

struct RuntimeLifecycleControlResult {
  std::string requestId;
  LifecycleAction action;
  RuntimeState runtimeState;
};

struct RootPresented {
  std::string surfaceId;
};

}  // namespace quickapp::android

