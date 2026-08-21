#include "quickapp/android/composition.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <utility>

namespace quickapp::android {
namespace {

RuntimeError invalid(std::string message) {
  return error(ErrorCode::kAbiInvalidArgument, std::move(message));
}

const JsonValue* field(const JsonValue::Object& object, std::string_view key) {
  const auto found = object.find(key);
  return found == object.end() ? nullptr : &found->second;
}

bool onlyKeys(const JsonValue::Object& object,
              const std::set<std::string, std::less<>>& allowed) {
  return std::ranges::all_of(object, [&allowed](const auto& entry) {
    return allowed.contains(entry.first);
  });
}

Result<std::vector<std::string>> stringArray(const JsonValue* value,
                                             std::string_view name) {
  if (value == nullptr || value->asArray() == nullptr) {
    return Result<std::vector<std::string>>::failure(
        invalid(std::string(name) + " must be an array"));
  }
  std::vector<std::string> output;
  std::set<std::string, std::less<>> unique;
  for (const auto& item : *value->asArray()) {
    const auto* text = item.asString();
    if (text == nullptr || text->empty() || !unique.insert(*text).second) {
      return Result<std::vector<std::string>>::failure(
          invalid(std::string(name) + " contains an invalid or duplicate item"));
    }
    output.push_back(*text);
  }
  return Result<std::vector<std::string>>::success(std::move(output));
}

bool contains(const std::vector<std::string>& values, std::string_view value) {
  return std::ranges::find(values, value) != values.end();
}

}  // namespace

Result<RuntimeCompositionManifest> decodeRuntimeCompositionManifest(
    std::string_view json) {
  auto parsed = parseJson(json);
  if (!parsed.ok()) {
    return Result<RuntimeCompositionManifest>::failure(parsed.error());
  }
  const auto* object = parsed.value().asObject();
  if (object == nullptr ||
      !onlyKeys(*object, {"schemaVersion", "kind", "profileId", "target",
                         "runtimeAbi", "conformance", "buildMode",
                         "observationLevel", "jsEngine", "linkedModules",
                         "components", "capabilities", "binaryBytes",
                         "staticMemoryBytes"})) {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("composition manifest must be a strict object"));
  }

  const auto text = [object](std::string_view name) -> const std::string* {
    const auto* value = field(*object, name);
    return value == nullptr ? nullptr : value->asString();
  };
  const auto number = [object](std::string_view name) -> const JsonNumber* {
    const auto* value = field(*object, name);
    return value == nullptr ? nullptr : value->asNumber();
  };
  const auto* schemaVersion = number("schemaVersion");
  const auto* kind = text("kind");
  const auto* profileId = text("profileId");
  const auto* target = text("target");
  const auto* runtimeAbi = text("runtimeAbi");
  const auto* conformanceText = text("conformance");
  const auto* buildModeText = text("buildMode");
  const auto* observationText = text("observationLevel");
  const auto* binaryBytes = number("binaryBytes");
  if (schemaVersion == nullptr || !schemaVersion->integer ||
      schemaVersion->value != 1 || kind == nullptr ||
      *kind != "runtimeCompositionManifest" || profileId == nullptr ||
      target == nullptr || *target != "android" || runtimeAbi == nullptr ||
      *runtimeAbi != "quickapp-kit-runtime-v1" ||
      conformanceText == nullptr || buildModeText == nullptr ||
      observationText == nullptr || binaryBytes == nullptr ||
      !binaryBytes->integer || binaryBytes->value < 1) {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("composition manifest header is invalid"));
  }
  static const std::regex kProfilePattern(
      "^[a-z0-9]+([.-][a-z0-9]+)*$");
  if (!std::regex_match(*profileId, kProfilePattern)) {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("profileId is invalid"));
  }

  Conformance conformance;
  if (*conformanceText == "v1") {
    conformance = Conformance::kV1;
  } else if (*conformanceText == "custom") {
    conformance = Conformance::kCustom;
  } else {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("conformance is invalid"));
  }
  BuildMode buildMode;
  if (*buildModeText == "debug") {
    buildMode = BuildMode::kDebug;
  } else if (*buildModeText == "release") {
    buildMode = BuildMode::kRelease;
  } else {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("buildMode is invalid"));
  }
  ObservationLevel observation;
  if (*observationText == "off") {
    observation = ObservationLevel::kOff;
  } else if (*observationText == "baseline") {
    observation = ObservationLevel::kBaseline;
  } else if (*observationText == "diagnostic") {
    observation = ObservationLevel::kDiagnostic;
  } else {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("observationLevel is invalid"));
  }
  if (conformance == Conformance::kV1 &&
      observation == ObservationLevel::kOff) {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("v1 conformance cannot disable observation"));
  }

  const auto* engineValue = field(*object, "jsEngine");
  const auto* engine = engineValue == nullptr ? nullptr : engineValue->asObject();
  if (engine == nullptr ||
      !onlyKeys(*engine,
                {"engineId", "engineVersion", "engineAbi", "moduleId"})) {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("jsEngine is invalid"));
  }
  const auto engineText = [engine](std::string_view name) -> const std::string* {
    const auto* value = field(*engine, name);
    return value == nullptr ? nullptr : value->asString();
  };
  const auto* engineId = engineText("engineId");
  const auto* engineVersion = engineText("engineVersion");
  const auto* engineAbi = engineText("engineAbi");
  const auto* engineModuleId = engineText("moduleId");
  if (engineId == nullptr || engineId->empty() || engineVersion == nullptr ||
      engineVersion->empty() || engineAbi == nullptr ||
      *engineAbi != "quickapp-kit-js-engine-v1" ||
      engineModuleId == nullptr ||
      !engineModuleId->starts_with("engine.")) {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("jsEngine identity is invalid"));
  }

  const auto* modulesValue = field(*object, "linkedModules");
  const auto* moduleArray =
      modulesValue == nullptr ? nullptr : modulesValue->asArray();
  if (moduleArray == nullptr || moduleArray->size() < 8) {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("linkedModules is invalid"));
  }
  std::vector<LinkedModule> modules;
  std::set<std::string, std::less<>> moduleIds;
  std::map<std::string, std::string, std::less<>> moduleCategories;
  std::size_t engineCount = 0;
  for (const auto& value : *moduleArray) {
    const auto* module = value.asObject();
    if (module == nullptr ||
        !onlyKeys(*module, {"moduleId", "category", "version"})) {
      return Result<RuntimeCompositionManifest>::failure(
          invalid("linked module is invalid"));
    }
    const auto* moduleIdValue = field(*module, "moduleId");
    const auto* categoryValue = field(*module, "category");
    const auto* versionValue = field(*module, "version");
    const auto* moduleId =
        moduleIdValue == nullptr ? nullptr : moduleIdValue->asString();
    const auto* category =
        categoryValue == nullptr ? nullptr : categoryValue->asString();
    const auto* version =
        versionValue == nullptr ? nullptr : versionValue->asString();
    if (moduleId == nullptr || moduleId->empty() || category == nullptr ||
        category->empty() || !moduleIds.insert(*moduleId).second) {
      return Result<RuntimeCompositionManifest>::failure(
          invalid("linked module identity is invalid"));
    }
    if (*category == "engine") {
      ++engineCount;
      if (*moduleId != *engineModuleId) {
        return Result<RuntimeCompositionManifest>::failure(
            invalid("selected engine module does not match manifest"));
      }
    }
    static const std::set<std::string, std::less<>> kCategories = {
        "kernel", "runtime", "engine", "platform", "backend",
        "component", "capability", "diagnostic"};
    if (!kCategories.contains(*category)) {
      return Result<RuntimeCompositionManifest>::failure(
          invalid("linked module category is invalid"));
    }
    moduleCategories.emplace(*moduleId, *category);
    modules.push_back(
        LinkedModule{*moduleId, *category, version == nullptr ? "" : *version});
  }

  const std::vector<std::string> kernels = {
      "kernel.bridge",       "kernel.render", "kernel.event",
      "kernel.lifecycle",    "kernel.runtime-tree",
      "kernel.transaction",  "runtime.js-framework"};
  for (const auto& required : kernels) {
    if (!moduleIds.contains(required)) {
      return Result<RuntimeCompositionManifest>::failure(
          invalid("required runtime module is missing"));
    }
  }
  for (const auto& kernel :
       {"kernel.bridge", "kernel.render", "kernel.event", "kernel.lifecycle",
        "kernel.runtime-tree", "kernel.transaction"}) {
    if (moduleCategories.at(kernel) != "kernel") {
      return Result<RuntimeCompositionManifest>::failure(
          invalid("fixed kernel module category is invalid"));
    }
  }
  if (moduleCategories.at("runtime.js-framework") != "runtime") {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("JS framework module category is invalid"));
  }
  if (engineCount != 1) {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("exactly one engine module is required"));
  }

  auto components = stringArray(field(*object, "components"), "components");
  auto capabilities =
      stringArray(field(*object, "capabilities"), "capabilities");
  if (!components.ok()) {
    return Result<RuntimeCompositionManifest>::failure(components.error());
  }
  if (!capabilities.ok()) {
    return Result<RuntimeCompositionManifest>::failure(capabilities.error());
  }
  if (conformance == Conformance::kV1 &&
      (!contains(components.value(), "View") ||
       !contains(components.value(), "Text") ||
       !contains(components.value(), "Button") ||
       !contains(capabilities.value(), "system.router") ||
       !contains(capabilities.value(), "system.prompt") ||
       !contains(capabilities.value(), "system.device"))) {
    return Result<RuntimeCompositionManifest>::failure(
        invalid("v1 conformance set is incomplete"));
  }

  return Result<RuntimeCompositionManifest>::success(
      RuntimeCompositionManifest{
          *profileId,
          *runtimeAbi,
          conformance,
          buildMode,
          observation,
          JsEngineIdentity{*engineId, *engineVersion, *engineAbi,
                           *engineModuleId},
          std::move(modules),
          std::move(components.value()),
          std::move(capabilities.value()),
          static_cast<std::uint64_t>(binaryBytes->value),
      });
}

Result<ComposedRuntime> AndroidCompositionRoot::compose(
    RuntimeCompositionManifest manifest,
    const CompositionSelection& selection) {
  if (selection.engineProviders.size() != 1 ||
      selection.engineProviders.front() == nullptr) {
    return Result<ComposedRuntime>::failure(
        invalid("exactly one JS engine provider must be selected"));
  }
  auto provider = selection.engineProviders.front();
  if (provider->identity() != manifest.jsEngine) {
    return Result<ComposedRuntime>::failure(error(
        ErrorCode::kModuleAbiUnsupported,
        "selected JS engine provider does not match composition manifest"));
  }

  std::set<std::string, std::less<>> expectedModules;
  for (const auto& module : manifest.linkedModules) {
    expectedModules.insert(module.moduleId);
  }
  const std::set<std::string, std::less<>> actualModules(
      selection.linkedModuleIds.begin(), selection.linkedModuleIds.end());
  if (selection.linkedModuleIds.size() != actualModules.size() ||
      expectedModules != actualModules ||
      selection.binaryBytes != manifest.binaryBytes) {
    return Result<ComposedRuntime>::failure(error(
        ErrorCode::kRuntimeProfileIncompatible,
        "composition manifest does not match build link inventory"));
  }

  std::shared_ptr<TraceSink> sink;
  if (manifest.observationLevel == ObservationLevel::kOff) {
    if (manifest.conformance != Conformance::kCustom ||
        selection.noopTraceSink == nullptr) {
      return Result<ComposedRuntime>::failure(
          invalid("off observation requires custom profile and NoopTraceSink"));
    }
    sink = selection.noopTraceSink;
  } else {
    if (selection.recordingTraceSink == nullptr) {
      return Result<ComposedRuntime>::failure(
          invalid("baseline observation requires a trace sink adapter"));
    }
    sink = selection.recordingTraceSink;
  }
  return Result<ComposedRuntime>::success(
      ComposedRuntime{std::move(manifest), std::move(provider),
                      std::move(sink)});
}

}  // namespace quickapp::android
