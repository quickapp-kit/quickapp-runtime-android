#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "quickapp/android/composition.h"
#include "quickapp/android/executor.h"
#include "quickapp/android/launch_profile.h"
#include "quickapp/android/package_source.h"
#include "quickapp/android/runtime_host.h"

namespace qa = quickapp::android;

namespace {

class TestFailure : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      throw TestFailure(std::string("check failed: ") + #condition +        \
                        " at line " + std::to_string(__LINE__));             \
    }                                                                        \
  } while (false)

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> output;
  output.reserve(text.size());
  for (const char value : text) {
    output.push_back(static_cast<std::byte>(value));
  }
  return output;
}

std::string text(const qa::ImmutableBytes& input) {
  std::string output;
  output.reserve(input->size());
  for (const auto value : *input) {
    output.push_back(static_cast<char>(value));
  }
  return output;
}

constexpr std::string_view kProfile = R"json({
  "artifact": "/tmp/case-001.rpk",
  "entryRoute": "/pages/index",
  "params": {"message": "hello", "count": 1},
  "viewport": {"width": 360, "height": 640, "unit": "logical-px"},
  "traceOutput": "disabled",
  "target": "android"
})json";

constexpr std::string_view kManifest = R"json({
  "schemaVersion": 1,
  "kind": "runtimeCompositionManifest",
  "profileId": "android-v1-debug",
  "target": "android",
  "runtimeAbi": "quickapp-kit-runtime-v1",
  "conformance": "v1",
  "buildMode": "debug",
  "observationLevel": "baseline",
  "jsEngine": {
    "engineId": "quickjs",
    "engineVersion": "1",
    "engineAbi": "quickapp-kit-js-engine-v1",
    "moduleId": "engine.quickjs"
  },
  "linkedModules": [
    {"moduleId": "kernel.bridge", "category": "kernel"},
    {"moduleId": "kernel.render", "category": "kernel"},
    {"moduleId": "kernel.event", "category": "kernel"},
    {"moduleId": "kernel.lifecycle", "category": "kernel"},
    {"moduleId": "kernel.runtime-tree", "category": "kernel"},
    {"moduleId": "kernel.transaction", "category": "kernel"},
    {"moduleId": "runtime.js-framework", "category": "runtime"},
    {"moduleId": "engine.quickjs", "category": "engine"}
  ],
  "components": ["View", "Text", "Button"],
  "capabilities": ["system.router", "system.prompt", "system.device"],
  "binaryBytes": 1
})json";

class FakeEngine final : public qa::JsEngineProvider {
 public:
  explicit FakeEngine(qa::JsEngineIdentity identity)
      : identity_(std::move(identity)) {}
  const qa::JsEngineIdentity& identity() const noexcept override {
    return identity_;
  }

 private:
  qa::JsEngineIdentity identity_;
};

class RecordingSink final : public qa::TraceSink {
 public:
  ~RecordingSink() override = default;
};

class FakeAssetReader final : public qa::AssetReader {
 public:
  explicit FakeAssetReader(std::span<const std::byte> input)
      : backend_(input) {}
  std::uint64_t size() const noexcept override { return backend_.size(); }
  qa::Result<qa::ImmutableBytes> read(std::uint64_t offset,
                                      std::size_t length) override {
    return backend_.read(offset, length);
  }
  void close() noexcept override {
    closed = true;
    backend_.close();
  }
  bool closed = false;

 private:
  qa::MemoryPackageBackend backend_;
};

class FakeCoreRuntime final : public qa::CoreAppRuntime {
 public:
  explicit FakeCoreRuntime(qa::ManualExecutor& executor)
      : executor_(executor) {}

  void startRoot(const qa::RuntimeLaunchProfile& profile,
                 qa::Completion<qa::RootPresented> completion) override {
    calls.push_back("startRoot:" + profile.entryRoute);
    executor_.post([this, completion = std::move(completion)]() mutable {
      completion(rootResult);
    });
  }

  void controlLifecycle(
      qa::RuntimeLifecycleControl control,
      qa::Completion<qa::RuntimeLifecycleControlResult> completion) override {
    calls.push_back("lifecycle:" + control.requestId);
    const auto action = control.action;
    const auto requestId = control.requestId;
    executor_.post([this, action, requestId,
                    completion = std::move(completion)]() mutable {
      if (nextLifecycleError.has_value()) {
        const auto error = *nextLifecycleError;
        nextLifecycleError.reset();
        completion(
            qa::Result<qa::RuntimeLifecycleControlResult>::failure(error));
        return;
      }
      const qa::RuntimeState state =
          action == qa::LifecycleAction::kEnterForeground
              ? qa::RuntimeState::kForeground
              : action == qa::LifecycleAction::kEnterBackground
                    ? qa::RuntimeState::kBackground
                    : qa::RuntimeState::kDestroyed;
      completion(qa::Result<qa::RuntimeLifecycleControlResult>::success(
          qa::RuntimeLifecycleControlResult{requestId, action, state}));
    });
  }

  qa::Result<qa::RootPresented> rootResult =
      qa::Result<qa::RootPresented>::success(
          qa::RootPresented{"srf:core-owned-root"});
  std::optional<qa::RuntimeError> nextLifecycleError;
  std::vector<std::string> calls;

 private:
  qa::ManualExecutor& executor_;
};

class FakeCoreFactory final : public qa::CoreAppRuntimeFactory {
 public:
  FakeCoreFactory(qa::ManualExecutor& executor,
                  std::shared_ptr<FakeCoreRuntime> runtime)
      : executor_(executor), runtime_(std::move(runtime)) {}

  void create(
      qa::AppRuntimeCreateRequest request,
      qa::Completion<std::shared_ptr<qa::CoreAppRuntime>> completion) override {
    ++createCount;
    receivedRequest = std::move(request);
    // The Core factory owns identity creation. No AppRuntimeId exists in request.
    coreGeneratedAppRuntimeId = "app:core-generated-1";
    executor_.post([this, completion = std::move(completion)]() mutable {
      completion(qa::Result<std::shared_ptr<qa::CoreAppRuntime>>::success(
          runtime_));
    });
  }

  int createCount = 0;
  std::string coreGeneratedAppRuntimeId;
  std::optional<qa::AppRuntimeCreateRequest> receivedRequest;

 private:
  qa::ManualExecutor& executor_;
  std::shared_ptr<FakeCoreRuntime> runtime_;
};

qa::ComposedRuntime compose(qa::RuntimeCompositionManifest manifest,
                            std::shared_ptr<qa::TraceSink> sink) {
  auto engine = std::make_shared<FakeEngine>(manifest.jsEngine);
  std::vector<std::string> moduleIds;
  for (const auto& module : manifest.linkedModules) {
    moduleIds.push_back(module.moduleId);
  }
  qa::CompositionSelection selection{{engine},
                                     std::make_shared<qa::NoopTraceSink>(),
                                     std::move(sink),
                                     std::move(moduleIds),
                                     manifest.binaryBytes};
  auto result =
      qa::AndroidCompositionRoot::compose(std::move(manifest), selection);
  CHECK(result.ok());
  return std::move(result.value());
}

void testStrictDecodersAndComposition() {
  auto profile = qa::decodeRuntimeLaunchProfile(kProfile);
  CHECK(profile.ok());
  CHECK(profile.value().entryRoute == "/pages/index");
  CHECK(profile.value().viewport.width == 360);

  auto unknown = qa::decodeRuntimeLaunchProfile(R"json({
    "artifact":"/tmp/a.rpk","params":{},
    "viewport":{"width":1,"height":1,"unit":"logical-px"},
    "traceOutput":"disabled","target":"android","private":true
  })json");
  CHECK(!unknown.ok());

  auto manifest = qa::decodeRuntimeCompositionManifest(kManifest);
  CHECK(manifest.ok());
  CHECK(manifest.value().linkedModules.size() == 8);

  auto engine = std::make_shared<FakeEngine>(manifest.value().jsEngine);
  std::vector<std::string> moduleIds;
  for (const auto& module : manifest.value().linkedModules) {
    moduleIds.push_back(module.moduleId);
  }
  qa::CompositionSelection zeroEngine{
      {}, std::make_shared<qa::NoopTraceSink>(),
      std::make_shared<RecordingSink>(), moduleIds,
      manifest.value().binaryBytes};
  CHECK(!qa::AndroidCompositionRoot::compose(manifest.value(), zeroEngine).ok());

  qa::CompositionSelection twoEngines{
      {engine, engine}, std::make_shared<qa::NoopTraceSink>(),
      std::make_shared<RecordingSink>(), moduleIds,
      manifest.value().binaryBytes};
  CHECK(
      !qa::AndroidCompositionRoot::compose(manifest.value(), twoEngines).ok());

  auto wrongIdentity = manifest.value().jsEngine;
  wrongIdentity.engineAbi = "wrong";
  qa::CompositionSelection wrongEngine{
      {std::make_shared<FakeEngine>(wrongIdentity)},
      std::make_shared<qa::NoopTraceSink>(),
      std::make_shared<RecordingSink>(), moduleIds,
      manifest.value().binaryBytes};
  auto mismatch =
      qa::AndroidCompositionRoot::compose(manifest.value(), wrongEngine);
  CHECK(!mismatch.ok());
  CHECK(mismatch.error().code == qa::ErrorCode::kModuleAbiUnsupported);

  auto customOff = manifest.value();
  customOff.conformance = qa::Conformance::kCustom;
  customOff.observationLevel = qa::ObservationLevel::kOff;
  qa::CompositionSelection offSelection{
      {engine}, std::make_shared<qa::NoopTraceSink>(), nullptr, moduleIds,
      customOff.binaryBytes};
  CHECK(
      qa::AndroidCompositionRoot::compose(customOff, offSelection).ok());

  auto wrongInventory = offSelection;
  wrongInventory.linkedModuleIds.pop_back();
  auto inventoryMismatch =
      qa::AndroidCompositionRoot::compose(customOff, wrongInventory);
  CHECK(!inventoryMismatch.ok());
  CHECK(inventoryMismatch.error().code ==
        qa::ErrorCode::kRuntimeProfileIncompatible);

  std::cout << "EVIDENCE composition: strict profile/manifest, one engine, "
               "runtime.js-framework once\n";
}

void runRead(qa::PackageSource& source, qa::ManualExecutor& io,
             qa::ManualExecutor& core, std::uint64_t offset,
             std::size_t length, qa::Result<qa::ImmutableBytes>* output,
             int* completions) {
  source.readAt(offset, length,
                [output, completions](qa::Result<qa::ImmutableBytes> result) {
                  *output = std::move(result);
                  ++*completions;
                });
  CHECK(*completions == 0);
  io.runAll();
  CHECK(*completions == 0);
  core.runAll();
}

void testPackageSources() {
  qa::ManualExecutor io;
  qa::ManualExecutor core;
  auto input = bytes("0123456789");
  auto memoryBackend = std::make_shared<qa::MemoryPackageBackend>(input);
  qa::AsyncPackageSource memory(memoryBackend, io, core);
  input[3] = static_cast<std::byte>('x');

  qa::Result<qa::ImmutableBytes> result =
      qa::Result<qa::ImmutableBytes>::failure(
          qa::error(qa::ErrorCode::kPackageIoError, "unset"));
  int completions = 0;
  runRead(memory, io, core, 2, 4, &result, &completions);
  CHECK(result.ok());
  CHECK(text(result.value()) == "2345");
  CHECK(completions == 1);

  completions = 0;
  runRead(memory, io, core, 10, 0, &result, &completions);
  CHECK(result.ok());
  CHECK(result.value()->empty());

  completions = 0;
  memory.readAt(9, 2, [&result, &completions](
                          qa::Result<qa::ImmutableBytes> readResult) {
    result = std::move(readResult);
    ++completions;
  });
  core.runAll();
  CHECK(completions == 1);
  CHECK(!result.ok());

  memory.readAt(0, 2, [&completions](qa::Result<qa::ImmutableBytes> closed) {
    CHECK(!closed.ok());
    ++completions;
  });
  memory.close();
  io.runAll();
  core.runAll();
  CHECK(completions == 2);

  const auto filePath =
      std::filesystem::temp_directory_path() / "and-s01-package-source.rpk";
  {
    std::ofstream output(filePath, std::ios::binary);
    output << "abcdefgh";
  }
  auto fileBackend = qa::FilePackageBackend::open(filePath);
  CHECK(fileBackend.ok());
  qa::AsyncPackageSource file(fileBackend.value(), io, core);
  completions = 0;
  runRead(file, io, core, 3, 3, &result, &completions);
  CHECK(result.ok());
  CHECK(text(result.value()) == "def");
  file.close();
  std::filesystem::remove(filePath);

  auto assetReader = std::make_shared<FakeAssetReader>(
      std::span<const std::byte>(bytes("asset-data")));
  auto assetBackend =
      std::make_shared<qa::AssetPackageBackend>(assetReader);
  qa::AsyncPackageSource asset(assetBackend, io, core);
  completions = 0;
  runRead(asset, io, core, 6, 4, &result, &completions);
  CHECK(result.ok());
  CHECK(text(result.value()) == "data");
  asset.close();
  CHECK(assetReader->closed);

  std::cout << "EVIDENCE package: memory/file/asset random read, immutable "
               "bytes, async completion, close race\n";
}

void testFileIdentityAndReadCloseRace() {
  qa::ManualExecutor io;
  qa::ManualExecutor core;
  const auto directory = std::filesystem::temp_directory_path();
  const auto packagePath = directory / "and-s01-fixed-identity.rpk";
  const auto openedResourcePath =
      directory / "and-s01-fixed-identity.opened.rpk";
  std::filesystem::remove(packagePath);
  std::filesystem::remove(openedResourcePath);

  {
    std::ofstream output(packagePath, std::ios::binary);
    output << "original";
  }
  auto backend = qa::FilePackageBackend::open(packagePath);
  CHECK(backend.ok());
  auto source =
      std::make_shared<qa::AsyncPackageSource>(backend.value(), io, core);

  std::filesystem::rename(packagePath, openedResourcePath);
  {
    std::ofstream replacement(packagePath, std::ios::binary);
    replacement << "replaced";
  }

  qa::Result<qa::ImmutableBytes> result =
      qa::Result<qa::ImmutableBytes>::failure(
          qa::error(qa::ErrorCode::kPackageIoError, "unset"));
  int completions = 0;
  runRead(*source, io, core, 0, 8, &result, &completions);
  CHECK(result.ok());
  CHECK(text(result.value()) == "original");
  CHECK(completions == 1);

  std::filesystem::resize_file(openedResourcePath, 2);
  completions = 0;
  runRead(*source, io, core, 0, 8, &result, &completions);
  CHECK(!result.ok());
  CHECK(result.error().code == qa::ErrorCode::kPackageIoError);
  CHECK(completions == 1);

  completions = 0;
  source->readAt(
      0, 1, [&result, &completions](qa::Result<qa::ImmutableBytes> read) {
        result = std::move(read);
        ++completions;
      });
  CHECK(io.pending() == 1);
  source->close();
  io.runAll();
  CHECK(completions == 0);
  core.runAll();
  CHECK(completions == 1);
  CHECK(!result.ok());
  CHECK(result.error().code == qa::ErrorCode::kPackageIoError);

  std::filesystem::remove(packagePath);
  std::filesystem::remove(openedResourcePath);
  std::cout << "EVIDENCE file identity: path replacement keeps original fd; "
               "truncate and read/close race fail once on Core queue\n";
}

struct HostFixture {
  HostFixture() {
    auto decodedProfile = qa::decodeRuntimeLaunchProfile(kProfile);
    auto decodedManifest = qa::decodeRuntimeCompositionManifest(kManifest);
    CHECK(decodedProfile.ok());
    CHECK(decodedManifest.ok());
    profile = std::move(decodedProfile.value());
    manifest = std::move(decodedManifest.value());
    runtime = std::make_shared<FakeCoreRuntime>(core);
    factory = std::make_shared<FakeCoreFactory>(core, runtime);
    backend = std::make_shared<qa::MemoryPackageBackend>(bytes("rpk"));
    source = std::make_shared<qa::AsyncPackageSource>(backend, io, core);
  }

  std::shared_ptr<qa::AndroidRuntimeHost> makeHost(
      std::shared_ptr<qa::TraceSink> sink) {
    auto host = qa::AndroidRuntimeHost::create(
        profile, compose(manifest, std::move(sink)), source, factory);
    CHECK(host.ok());
    return std::move(host.value());
  }

  qa::ManualExecutor io;
  qa::ManualExecutor core;
  qa::RuntimeLaunchProfile profile{
      "", "", {}, qa::Viewport{1, 1}, "disabled"};
  qa::RuntimeCompositionManifest manifest;
  std::shared_ptr<FakeCoreRuntime> runtime;
  std::shared_ptr<FakeCoreFactory> factory;
  std::shared_ptr<qa::MemoryPackageBackend> backend;
  std::shared_ptr<qa::AsyncPackageSource> source;
};

void testRootPresentedAndCoreOwnedIdentity() {
  HostFixture fixture;
  auto sink = std::make_shared<RecordingSink>();
  auto host = fixture.makeHost(sink);
  bool completed = false;
  host->start([&completed](qa::Result<qa::RootPresented> result) {
    CHECK(result.ok());
    CHECK(result.value().surfaceId == "srf:core-owned-root");
    completed = true;
  });
  CHECK(!completed);
  CHECK(host->state() == qa::HostState::kStarting);
  CHECK(fixture.core.runOne());
  CHECK(!completed);
  CHECK(fixture.factory->createCount == 1);
  CHECK(fixture.factory->coreGeneratedAppRuntimeId == "app:core-generated-1");
  CHECK(fixture.core.runOne());
  CHECK(completed);
  CHECK(host->state() == qa::HostState::kRunning);

  bool duplicateCompleted = false;
  host->start([&duplicateCompleted](qa::Result<qa::RootPresented> result) {
    CHECK(!result.ok());
    duplicateCompleted = true;
  });
  CHECK(duplicateCompleted);
  CHECK(fixture.factory->createCount == 1);

  std::cout << "EVIDENCE root: success only after Core presented; "
               "AppRuntimeId generated by Fake Core factory\n";
}

void testLifecycleBusyAndDestroy() {
  HostFixture fixture;
  auto host = fixture.makeHost(std::make_shared<RecordingSink>());
  bool started = false;
  host->start([&started](qa::Result<qa::RootPresented> result) {
    CHECK(result.ok());
    started = true;
  });
  fixture.core.runAll();
  CHECK(started);

  fixture.runtime->nextLifecycleError =
      qa::error(qa::ErrorCode::kLifecycleBusy, "busy");
  bool lifecycleCompleted = false;
  host->controlLifecycle(
      qa::RuntimeLifecycleControl{"req:foreground-1",
                                  qa::LifecycleAction::kEnterForeground},
      [&lifecycleCompleted](
          qa::Result<qa::RuntimeLifecycleControlResult> result) {
        CHECK(!result.ok());
        CHECK(result.error().code == qa::ErrorCode::kLifecycleBusy);
        lifecycleCompleted = true;
      });
  fixture.core.runAll();
  CHECK(lifecycleCompleted);
  CHECK(host->state() == qa::HostState::kRunning);

  bool destroyed = false;
  host->destroy(
      "req:destroy-1",
      [&destroyed](qa::Result<qa::RuntimeLifecycleControlResult> result) {
        CHECK(result.ok());
        CHECK(result.value().runtimeState == qa::RuntimeState::kDestroyed);
        destroyed = true;
      });
  CHECK(host->state() == qa::HostState::kDestroying);

  bool concurrentDestroyCompleted = false;
  host->destroy(
      "req:destroy-2",
      [&concurrentDestroyCompleted](
          qa::Result<qa::RuntimeLifecycleControlResult> result) {
        CHECK(!result.ok());
        CHECK(result.error().code == qa::ErrorCode::kLifecycleBusy);
        concurrentDestroyCompleted = true;
      });
  CHECK(concurrentDestroyCompleted);
  fixture.core.runAll();
  CHECK(destroyed);
  CHECK(host->state() == qa::HostState::kDestroyed);

  qa::Result<qa::ImmutableBytes> afterClose =
      qa::Result<qa::ImmutableBytes>::success(
          std::make_shared<const std::vector<std::byte>>());
  int completions = 0;
  fixture.source->readAt(
      0, 1, [&afterClose, &completions](qa::Result<qa::ImmutableBytes> result) {
        afterClose = std::move(result);
        ++completions;
      });
  fixture.core.runAll();
  CHECK(completions == 1);
  CHECK(!afterClose.ok());

  std::cout << "EVIDENCE lifecycle: LIFECYCLE_BUSY forwarded; destroy closes "
               "source and releases host resources\n";
}

void testFailureAndDestroyRaces() {
  {
    HostFixture fixture;
    fixture.runtime->rootResult =
        qa::Result<qa::RootPresented>::failure(qa::error(
            qa::ErrorCode::kPlatformRejected, "present failed"));
    auto host = fixture.makeHost(std::make_shared<RecordingSink>());
    bool startFailed = false;
    host->start([&startFailed](qa::Result<qa::RootPresented> result) {
      CHECK(!result.ok());
      CHECK(result.error().code == qa::ErrorCode::kPlatformRejected);
      startFailed = true;
    });
    CHECK(fixture.core.runOne());
    CHECK(fixture.core.runOne());
    CHECK(host->state() == qa::HostState::kDestroying);
    bool duplicateCleanupRejected = false;
    host->destroy(
        "req:duplicate-root-cleanup",
        [&duplicateCleanupRejected](
            qa::Result<qa::RuntimeLifecycleControlResult> result) {
          CHECK(!result.ok());
          CHECK(result.error().code == qa::ErrorCode::kLifecycleBusy);
          duplicateCleanupRejected = true;
        });
    CHECK(duplicateCleanupRejected);
    fixture.core.runAll();
    CHECK(startFailed);
    CHECK(host->state() == qa::HostState::kDestroyed);
  }

  {
    HostFixture fixture;
    auto host = fixture.makeHost(std::make_shared<RecordingSink>());
    bool startCancelled = false;
    host->start([&startCancelled](qa::Result<qa::RootPresented> result) {
      CHECK(!result.ok());
      startCancelled = true;
    });
    bool destroyed = false;
    host->destroy(
        "req:destroy-during-start",
        [&destroyed](qa::Result<qa::RuntimeLifecycleControlResult> result) {
          CHECK(result.ok());
          destroyed = true;
        });
    CHECK(destroyed);
    fixture.core.runAll();
    CHECK(startCancelled);
    CHECK(host->state() == qa::HostState::kDestroyed);
  }

  {
    HostFixture fixture;
    auto host = fixture.makeHost(std::make_shared<RecordingSink>());
    host->start(
        [](qa::Result<qa::RootPresented> result) { CHECK(result.ok()); });
    fixture.core.runAll();
    fixture.runtime->nextLifecycleError = qa::error(
        qa::ErrorCode::kPlatformRejected, "Core destroy failed");
    bool destroyFailed = false;
    host->destroy(
        "req:destroy-failure",
        [&destroyFailed](
            qa::Result<qa::RuntimeLifecycleControlResult> result) {
          CHECK(!result.ok());
          destroyFailed = true;
        });
    fixture.core.runAll();
    CHECK(destroyFailed);
    CHECK(host->state() == qa::HostState::kDestroyed);
  }

  std::cout << "EVIDENCE failure: present failure, destroy during start, and "
               "Core destroy failure all release local resources\n";
}

std::vector<std::string> runObservationScenario(
    std::shared_ptr<qa::TraceSink> sink) {
  HostFixture fixture;
  auto host = fixture.makeHost(std::move(sink));
  host->start([](qa::Result<qa::RootPresented> result) { CHECK(result.ok()); });
  fixture.core.runAll();
  host->controlLifecycle(
      qa::RuntimeLifecycleControl{"req:bg",
                                  qa::LifecycleAction::kEnterBackground},
      [](qa::Result<qa::RuntimeLifecycleControlResult> result) {
        CHECK(result.ok());
      });
  fixture.core.runAll();
  host->destroy(
      "req:done",
      [](qa::Result<qa::RuntimeLifecycleControlResult> result) {
        CHECK(result.ok());
      });
  fixture.core.runAll();
  return fixture.runtime->calls;
}

void testNoopRecordingEquivalence() {
  const auto noop =
      runObservationScenario(std::make_shared<qa::NoopTraceSink>());
  const auto recording =
      runObservationScenario(std::make_shared<RecordingSink>());
  CHECK(noop == recording);
  std::cout << "EVIDENCE observation: Noop/Recording preserve Core call order "
               "and results\n";
}

}  // namespace

int main() {
  const std::array<std::pair<std::string_view, std::function<void()>>, 7> tests{{
      {"strict decoders and composition", testStrictDecodersAndComposition},
      {"package sources", testPackageSources},
      {"file identity and read/close race", testFileIdentityAndReadCloseRace},
      {"root presented and Core identity", testRootPresentedAndCoreOwnedIdentity},
      {"lifecycle busy and destroy", testLifecycleBusyAndDestroy},
      {"failure and destroy races", testFailureAndDestroyRaces},
      {"Noop/Recording equivalence", testNoopRecordingEquivalence},
  }};

  int failures = 0;
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception& exception) {
      ++failures;
      std::cerr << "FAIL " << name << ": " << exception.what() << '\n';
    }
  }
  std::cout << "SUMMARY " << (tests.size() - failures) << '/' << tests.size()
            << " contract groups passed\n";
  return failures == 0 ? 0 : 1;
}
