#include "quickapp/android/launch_profile.h"

#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

namespace quickapp::android {
namespace {

RuntimeError invalid(std::string message) {
  return error(ErrorCode::kAbiInvalidArgument, std::move(message));
}

bool onlyKeys(const JsonValue::Object& object,
              const std::set<std::string, std::less<>>& allowed) {
  for (const auto& [key, unused] : object) {
    (void)unused;
    if (!allowed.contains(key)) {
      return false;
    }
  }
  return true;
}

const JsonValue* field(const JsonValue::Object& object, std::string_view key) {
  const auto found = object.find(key);
  return found == object.end() ? nullptr : &found->second;
}

bool isNormalizedRoute(std::string_view route) {
  return !route.empty() && route.front() == '/' &&
         route.find("//") == std::string_view::npos &&
         (route.size() == 1 || route.back() != '/');
}

}  // namespace

Result<RuntimeLaunchProfile> decodeRuntimeLaunchProfile(std::string_view json) {
  auto parsed = parseJson(json);
  if (!parsed.ok()) {
    return Result<RuntimeLaunchProfile>::failure(parsed.error());
  }
  const auto* object = parsed.value().asObject();
  if (object == nullptr ||
      !onlyKeys(*object, {"artifact", "entryRoute", "params", "viewport",
                         "traceOutput", "target"})) {
    return Result<RuntimeLaunchProfile>::failure(
        invalid("launch profile must be a strict object"));
  }

  const auto* artifactValue = field(*object, "artifact");
  const auto* paramsValue = field(*object, "params");
  const auto* viewportValue = field(*object, "viewport");
  const auto* traceValue = field(*object, "traceOutput");
  const auto* targetValue = field(*object, "target");
  if (artifactValue == nullptr || paramsValue == nullptr ||
      viewportValue == nullptr || traceValue == nullptr ||
      targetValue == nullptr) {
    return Result<RuntimeLaunchProfile>::failure(
        invalid("launch profile is missing a required field"));
  }

  const auto* artifact = artifactValue->asString();
  const auto* params = paramsValue->asObject();
  const auto* viewport = viewportValue->asObject();
  const auto* traceOutput = traceValue->asString();
  const auto* target = targetValue->asString();
  if (artifact == nullptr || artifact->empty() ||
      !std::filesystem::path(*artifact).is_absolute() || params == nullptr ||
      viewport == nullptr || traceOutput == nullptr || target == nullptr ||
      *target != "android") {
    return Result<RuntimeLaunchProfile>::failure(
        invalid("launch profile field type or target is invalid"));
  }
  if (!onlyKeys(*viewport, {"width", "height", "unit"})) {
    return Result<RuntimeLaunchProfile>::failure(
        invalid("viewport contains an unknown field"));
  }
  const auto* widthValue = field(*viewport, "width");
  const auto* heightValue = field(*viewport, "height");
  const auto* unitValue = field(*viewport, "unit");
  if (widthValue == nullptr || heightValue == nullptr || unitValue == nullptr ||
      widthValue->asNumber() == nullptr ||
      heightValue->asNumber() == nullptr || unitValue->asString() == nullptr ||
      *unitValue->asString() != "logical-px" ||
      widthValue->asNumber()->value <= 0.0 ||
      heightValue->asNumber()->value <= 0.0) {
    return Result<RuntimeLaunchProfile>::failure(invalid("invalid viewport"));
  }

  std::string route;
  if (const auto* routeValue = field(*object, "entryRoute");
      routeValue != nullptr) {
    const auto* routeString = routeValue->asString();
    if (routeString == nullptr || !isNormalizedRoute(*routeString)) {
      return Result<RuntimeLaunchProfile>::failure(
          invalid("entryRoute is not normalized"));
    }
    route = *routeString;
  }
  if (*traceOutput != "disabled" &&
      !std::filesystem::path(*traceOutput).is_absolute()) {
    return Result<RuntimeLaunchProfile>::failure(
        invalid("traceOutput must be disabled or resolved"));
  }

  return Result<RuntimeLaunchProfile>::success(RuntimeLaunchProfile{
      *artifact,
      std::move(route),
      *params,
      Viewport{widthValue->asNumber()->value, heightValue->asNumber()->value},
      *traceOutput,
  });
}

}  // namespace quickapp::android

