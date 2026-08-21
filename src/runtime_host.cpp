#include "quickapp/android/runtime_host.h"

#include <optional>
#include <utility>

namespace quickapp::android {
namespace {

RuntimeError rejected(std::string message) {
  return error(ErrorCode::kPlatformRejected, std::move(message));
}

RuntimeLifecycleControlResult destroyedResult(std::string requestId) {
  return RuntimeLifecycleControlResult{std::move(requestId),
                                       LifecycleAction::kDestroyAppRuntime,
                                       RuntimeState::kDestroyed};
}

}  // namespace

Result<std::shared_ptr<AndroidRuntimeHost>> AndroidRuntimeHost::create(
    RuntimeLaunchProfile profile, ComposedRuntime composition,
    std::shared_ptr<PackageSource> packageSource,
    std::shared_ptr<CoreAppRuntimeFactory> factory) {
  if (packageSource == nullptr || factory == nullptr ||
      composition.engineProvider == nullptr || composition.traceSink == nullptr) {
    return Result<std::shared_ptr<AndroidRuntimeHost>>::failure(
        error(ErrorCode::kAbiInvalidArgument,
              "runtime host dependencies must be non-null"));
  }
  return Result<std::shared_ptr<AndroidRuntimeHost>>::success(
      std::shared_ptr<AndroidRuntimeHost>(new AndroidRuntimeHost(
          std::move(profile), std::move(composition),
          std::move(packageSource), std::move(factory))));
}

AndroidRuntimeHost::AndroidRuntimeHost(
    RuntimeLaunchProfile profile, ComposedRuntime composition,
    std::shared_ptr<PackageSource> packageSource,
    std::shared_ptr<CoreAppRuntimeFactory> factory)
    : profile_(std::move(profile)),
      composition_(std::move(composition)),
      packageSource_(std::move(packageSource)),
      factory_(std::move(factory)) {}

void AndroidRuntimeHost::start(Completion<RootPresented> completion) {
  std::shared_ptr<CoreAppRuntimeFactory> factory;
  AppRuntimeCreateRequest request;
  bool rejectedStart = false;
  {
    std::lock_guard lock(mutex_);
    if (state_ != HostState::kNew) {
      rejectedStart = true;
    } else {
      state_ = HostState::kComposing;
      request = AppRuntimeCreateRequest{
          composition_.manifest, packageSource_, composition_.engineProvider,
          composition_.traceSink};
      factory = factory_;
      state_ = HostState::kStarting;
    }
  }
  if (rejectedStart) {
    completion(Result<RootPresented>::failure(
        rejected("runtime host can only start once")));
    return;
  }

  auto self = shared_from_this();
  factory->create(
      std::move(request),
      [self, completion = std::move(completion)](
          Result<std::shared_ptr<CoreAppRuntime>> created) mutable {
        std::shared_ptr<CoreAppRuntime> runtime;
        bool nullRuntime = false;
        {
          std::lock_guard lock(self->mutex_);
          if (self->state_ != HostState::kStarting) {
            runtime = created.ok() ? created.value() : nullptr;
          } else if (!created.ok()) {
            self->state_ = HostState::kFailed;
          } else if (created.value() == nullptr) {
            self->state_ = HostState::kFailed;
            nullRuntime = true;
          } else {
            self->runtime_ = created.value();
            runtime = self->runtime_;
          }
        }

        if (self->state() != HostState::kStarting) {
          if (runtime != nullptr) {
            runtime->controlLifecycle(
                RuntimeLifecycleControl{"req:android-cancel-create",
                                        LifecycleAction::kDestroyAppRuntime},
                [self](Result<RuntimeLifecycleControlResult>) {
                  self->finishLocalTeardown();
                });
          } else {
            self->finishLocalTeardown();
          }
          completion(Result<RootPresented>::failure(
              !created.ok()
                  ? created.error()
                  : nullRuntime ? rejected("Core factory returned null runtime")
                                : rejected("runtime start was cancelled")));
          return;
        }

        runtime->startRoot(
            self->profile_,
            [self, completion = std::move(completion)](
                Result<RootPresented> rootResult) mutable {
              std::shared_ptr<CoreAppRuntime> runtimeForCleanup;
              bool lateResult = false;
              {
                std::lock_guard lock(self->mutex_);
                if (self->state_ != HostState::kStarting) {
                  lateResult = true;
                } else if (rootResult.ok()) {
                  self->state_ = HostState::kRunning;
                } else {
                  self->state_ = HostState::kDestroying;
                  runtimeForCleanup = self->runtime_;
                }
              }
              if (lateResult) {
                completion(Result<RootPresented>::failure(
                    rejected("late root result after host teardown")));
                return;
              }
              if (rootResult.ok()) {
                completion(std::move(rootResult));
                return;
              }
              const RuntimeError rootError = rootResult.error();
              runtimeForCleanup->controlLifecycle(
                  RuntimeLifecycleControl{"req:android-start-failure-cleanup",
                                          LifecycleAction::kDestroyAppRuntime},
                  [self, completion = std::move(completion),
                   rootError](Result<RuntimeLifecycleControlResult>) mutable {
                    self->finishLocalTeardown();
                    completion(Result<RootPresented>::failure(rootError));
                  });
            });
      });
}

void AndroidRuntimeHost::controlLifecycle(
    RuntimeLifecycleControl control,
    Completion<RuntimeLifecycleControlResult> completion) {
  std::shared_ptr<CoreAppRuntime> runtime;
  bool rejectedControl = false;
  {
    std::lock_guard lock(mutex_);
    if (state_ != HostState::kRunning ||
        control.action == LifecycleAction::kDestroyAppRuntime) {
      rejectedControl = true;
    } else {
      runtime = runtime_;
    }
  }
  if (rejectedControl) {
    completion(Result<RuntimeLifecycleControlResult>::failure(
        rejected("host is not accepting this lifecycle control")));
    return;
  }
  runtime->controlLifecycle(std::move(control), std::move(completion));
}

void AndroidRuntimeHost::destroy(
    std::string requestId,
    Completion<RuntimeLifecycleControlResult> completion) {
  std::shared_ptr<CoreAppRuntime> runtime;
  std::optional<RuntimeError> immediateError;
  {
    std::lock_guard lock(mutex_);
    if (state_ == HostState::kDestroying) {
      immediateError =
          error(ErrorCode::kLifecycleBusy, "host destruction is in flight");
    } else if (state_ == HostState::kDestroyed) {
      immediateError = rejected("runtime host is already destroyed");
    } else {
      state_ = HostState::kDestroying;
      runtime = runtime_;
    }
  }
  if (immediateError.has_value()) {
    completion(Result<RuntimeLifecycleControlResult>::failure(
        std::move(*immediateError)));
    return;
  }

  if (runtime == nullptr) {
    finishLocalTeardown();
    completion(Result<RuntimeLifecycleControlResult>::success(
        destroyedResult(std::move(requestId))));
    return;
  }

  auto self = shared_from_this();
  runtime->controlLifecycle(
      RuntimeLifecycleControl{requestId,
                              LifecycleAction::kDestroyAppRuntime},
      [self, completion = std::move(completion)](
          Result<RuntimeLifecycleControlResult> result) mutable {
        self->finishLocalTeardown();
        completion(std::move(result));
      });
}

HostState AndroidRuntimeHost::state() const noexcept {
  std::lock_guard lock(mutex_);
  return state_;
}

const RuntimeCompositionManifest&
AndroidRuntimeHost::describeComposition() const noexcept {
  return composition_.manifest;
}

void AndroidRuntimeHost::finishLocalTeardown() noexcept {
  std::shared_ptr<PackageSource> packageSource;
  {
    std::lock_guard lock(mutex_);
    packageSource = std::move(packageSource_);
    runtime_.reset();
    factory_.reset();
    composition_.engineProvider.reset();
    composition_.traceSink.reset();
    state_ = HostState::kDestroyed;
  }
  if (packageSource != nullptr) {
    packageSource->close();
  }
}

}  // namespace quickapp::android
