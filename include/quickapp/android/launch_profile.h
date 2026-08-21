#pragma once

#include <string_view>

#include "quickapp/android/contracts.h"

namespace quickapp::android {

Result<RuntimeLaunchProfile> decodeRuntimeLaunchProfile(std::string_view json);

}  // namespace quickapp::android

