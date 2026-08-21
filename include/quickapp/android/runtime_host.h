#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "quickapp/android/composition.h"
#include "quickapp/android/contracts.h"

namespace quickapp::android {

template <typename T>
using Completion = std::function<void(Result<T>)>;

class CoreAppRuntime {
 public:
  virtual ~CoreAppRuntime() = default;
  virtual void startRoot(const RuntimeLaunchProfile& profile,
                         Completion<RootPresented> completion) = 0;
  virtual void controlLifecycle(RuntimeLifecycleControl control,
                                Completion<RuntimeLifecycleControlResult>
                                    completion) = 0;
};

// Intentionally contains no AppRuntimeId. Core creates and owns that identity.
struct AppRuntimeCreateRequest {
  RuntimeCompositionManifest manifest;
  std::shared_ptr<PackageSource> packageSource;
  std::shared_ptr<JsEngineProvider> engineProvider;
  std::shared_ptr<TraceSink> traceSink;
};

class CoreAppRuntimeFactory {
 public:
  virtual ~CoreAppRuntimeFactory() = default;
  virtual void create(AppRuntimeCreateRequest request,
                      Completion<std::shared_ptr<CoreAppRuntime>> completion) = 0;
};

enum class HostState {
  kNew,
  kComposing,
  kStarting,
  kRunning,
  kDestroying,
  kDestroyed,
  kFailed,
};

class AndroidRuntimeHost final
    : public std::enable_shared_from_this<AndroidRuntimeHost> {
 public:
  static Result<std::shared_ptr<AndroidRuntimeHost>> create(
      RuntimeLaunchProfile profile, ComposedRuntime composition,
      std::shared_ptr<PackageSource> packageSource,
      std::shared_ptr<CoreAppRuntimeFactory> factory);

  void start(Completion<RootPresented> completion);
  void controlLifecycle(
      RuntimeLifecycleControl control,
      Completion<RuntimeLifecycleControlResult> completion);
  void destroy(std::string requestId,
               Completion<RuntimeLifecycleControlResult> completion);

  HostState state() const noexcept;
  const RuntimeCompositionManifest& describeComposition() const noexcept;

 private:
  AndroidRuntimeHost(RuntimeLaunchProfile profile, ComposedRuntime composition,
                     std::shared_ptr<PackageSource> packageSource,
                     std::shared_ptr<CoreAppRuntimeFactory> factory);

  void finishLocalTeardown() noexcept;

  mutable std::mutex mutex_;
  RuntimeLaunchProfile profile_;
  ComposedRuntime composition_;
  std::shared_ptr<PackageSource> packageSource_;
  std::shared_ptr<CoreAppRuntimeFactory> factory_;
  std::shared_ptr<CoreAppRuntime> runtime_;
  HostState state_ = HostState::kNew;
};

}  // namespace quickapp::android

