#include "quickapp/android/runtime_spine.h"

#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>
#include <variant>

#include "quickapp/core/foundation/app_runtime_factory.h"
#include "quickapp/core/package/package_loader.h"
#include "quickapp/core/render/initial_render_pipeline.h"
#include "quickapp/core/surface/surface_controller.h"
#include "quickapp/js/abi/runtime_abi_service.h"
#include "quickapp/js/alpha/alpha_page_initialization_stage.h"
#include "quickapp/js/binding/alpha_initial_binding_stage.h"
#include "quickapp/js/engine/js_engine_service.h"
#include "quickapp/js/engine/observation.h"
#include "quickapp/js/engine/quickjs_engine_provider.h"
#include "quickapp/js/event/handler_registry.h"
#include "quickapp/js/framework/static_facade_catalog.h"
#include "quickapp/js/module/module_loader.h"
#include "quickapp/js/page/page_host_control.h"
#include "quickapp/js/render/alpha_initial_transaction_builder.h"
#include "quickapp/js/vm/vm_lifecycle_service.h"

namespace quickapp::android {
namespace {

namespace qc = core;
namespace qp = core::package;
namespace qr = core::render;
namespace qs = core::surface;
namespace qj = js;
namespace ja = js::abi;

class CoreMailbox final {
 public:
  explicit CoreMailbox(std::size_t capacity) : capacity_(capacity) {}

  bool post(std::function<void()> task) noexcept {
    try {
      std::lock_guard lock(mutex_);
      if (closed_ || tasks_.size() >= capacity_) return false;
      tasks_.push(std::move(task));
      ready_.notify_one();
      return true;
    } catch (...) {
      return false;
    }
  }

  std::size_t drain(std::size_t budget) noexcept {
    std::size_t count = 0;
    while (count < budget) {
      std::function<void()> task;
      {
        std::lock_guard lock(mutex_);
        if (tasks_.empty()) break;
        task = std::move(tasks_.front());
        tasks_.pop();
      }
      if (task) {
        try {
          task();
        } catch (...) {
        }
      }
      ++count;
    }
    return count;
  }

  void wait() noexcept {
    std::unique_lock lock(mutex_);
    ready_.wait_for(lock, std::chrono::milliseconds(10),
                    [this] { return closed_ || !tasks_.empty(); });
  }

  void close() noexcept {
    std::lock_guard lock(mutex_);
    closed_ = true;
    while (!tasks_.empty()) tasks_.pop();
    ready_.notify_all();
  }

  std::size_t depth() const noexcept {
    std::lock_guard lock(mutex_);
    return tasks_.size();
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<std::function<void()>> tasks_;
  bool closed_{false};
};

class MemorySource final : public qp::PackageSource {
 public:
  explicit MemorySource(qp::Bytes bytes)
      : bytes_(std::make_shared<const qp::Bytes>(std::move(bytes))) {}

  qc::RuntimeResult<std::uint64_t> size() noexcept override {
    return qc::RuntimeResult<std::uint64_t>::success(bytes_->size());
  }

  qc::EnqueueResult read_at(qp::PackageReadRequest request,
                            qp::PackageReadCompletion completion) noexcept override {
    if (!completion || closed_ || request.offset > bytes_->size() ||
        request.length > bytes_->size() - request.offset) {
      return qc::EnqueueResult::failure(qc::RuntimeError::simple(
          qc::RuntimeErrorCode::kPackageIoError, "Android package read rejected"));
    }
    try {
      auto value = std::make_shared<qp::Bytes>(
          bytes_->begin() + static_cast<std::ptrdiff_t>(request.offset),
          bytes_->begin() + static_cast<std::ptrdiff_t>(request.offset + request.length));
      completion(qp::PackageReadResult{
          std::move(request.request_id),
          qc::RuntimeResult<qp::ImmutableBytes>::success(std::move(value))});
      return qc::EnqueueResult::success(qc::Accepted{});
    } catch (...) {
      return qc::EnqueueResult::failure(qc::RuntimeError::simple(
          qc::RuntimeErrorCode::kOutOfMemory, "Android package read allocation failed"));
    }
  }

  void close() noexcept override { closed_ = true; }

 private:
  std::shared_ptr<const qp::Bytes> bytes_;
  bool closed_{false};
};

qp::Bytes readFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open Android Runtime RPK");
  input.seekg(0, std::ios::end);
  const auto end = input.tellg();
  input.seekg(0, std::ios::beg);
  if (end < 0) throw std::runtime_error("cannot stat Android Runtime RPK");
  qp::Bytes bytes(static_cast<std::size_t>(end));
  input.read(reinterpret_cast<char*>(bytes.data()), end);
  if (!input) throw std::runtime_error("cannot read Android Runtime RPK");
  return bytes;
}

qc::RequestId parseRequest(std::string value) {
  auto result = qc::RequestId::parse(std::move(value));
  if (!result) throw std::runtime_error("invalid Core RequestId");
  return std::move(result).value();
}

class Clock final : public qj::MonotonicClock {
 public:
  std::uint64_t nowNs() const noexcept override {
    return sequence_.fetch_add(1000, std::memory_order_relaxed);
  }

 private:
  mutable std::atomic<std::uint64_t> sequence_{1};
};

class TraceSink final : public qj::TraceSink {
 public:
  void emit(const qj::TraceEvent&) noexcept override {}
};

class ModuleCompletion final : public qj::module::ModuleCompletionPort {
 public:
  qj::module::ModuleEnqueueResult post(
      const qj::module::ModuleLoadCompletion&) noexcept override {
    return {qj::module::ModuleEnqueueStatus::Accepted};
  }
};

class RequestIds final : public qj::framework::JsRequestIdAllocatorPort {
 public:
  std::string nextRequestId() noexcept override {
    return "req:android-js-" + std::to_string(next_++);
  }

 private:
  std::uint64_t next_{1};
};

class PageResolver final : public qs::VerifiedPageResolver {
 public:
  PageResolver(qp::PackageLoader& loader,
               std::shared_ptr<const qp::VerifiedPackage> package)
      : loader_(loader), package_(std::move(package)) {}

  qc::RuntimeResult<qs::VerifiedSurfacePage> resolve(
      std::string_view route, const qc::SurfaceId& surface_id) noexcept override {
    const auto found = package_->pages().find(std::string(route));
    if (found == package_->pages().end()) {
      return qc::RuntimeResult<qs::VerifiedSurfacePage>::failure(
          qc::RuntimeError::simple(qc::RuntimeErrorCode::kRouteNotFound,
                                   "route is absent from Android Runtime RPK"));
    }
    std::optional<qp::VerifiedModule> module;
    std::optional<qp::PageIrHandle> page_ir;
    std::optional<qc::RuntimeError> failure;
    if (!loader_.load_module({found->second.module_id, surface_id}, [&](auto result) {
          if (result) module = std::move(result).value();
          else failure = result.error();
        }) || failure || !module) {
      return qc::RuntimeResult<qs::VerifiedSurfacePage>::failure(
          failure.value_or(qc::RuntimeError::simple(
              qc::RuntimeErrorCode::kPackageIoError, "page module load failed")));
    }
    if (!loader_.load_page_ir(std::string(route), [&](auto result) {
          if (result) page_ir = std::move(result).value();
          else failure = result.error();
        }) || failure || !page_ir) {
      return qc::RuntimeResult<qs::VerifiedSurfacePage>::failure(
          failure.value_or(qc::RuntimeError::simple(
              qc::RuntimeErrorCode::kPackageIoError, "page IR load failed")));
    }
    return qc::RuntimeResult<qs::VerifiedSurfacePage>::success(
        {std::string(route), std::move(*module), std::move(*page_ir)});
  }

 private:
  qp::PackageLoader& loader_;
  std::shared_ptr<const qp::VerifiedPackage> package_;
};

class AppState final : public qs::AppRuntimeStateView {
 public:
  qc::lifecycle::AppRuntimeState state() const noexcept override {
    return qc::lifecycle::AppRuntimeState::kForeground;
  }
};

class ControllerStatus final : public qs::SurfaceStatusSink {
 public:
  void status(qs::SurfaceStatusChanged) noexcept override {}
  void close() noexcept override {}
};

class ControllerLifecycleResults final : public qs::SurfaceLifecycleResultSink {
 public:
  void complete(qc::lifecycle::SurfaceLifecycleResult) noexcept override {}
  void close() noexcept override {}
};

class ControllerInitialResults final : public qr::InitialContentResultSink {
 public:
  void bind(qs::SurfaceController& controller) noexcept { controller_ = &controller; }
  void complete(qs::surface::InitialContentResult result) noexcept override {
    if (controller_) static_cast<void>(controller_->enqueue(std::move(result)));
  }
  void close() noexcept override {}

 private:
  qs::SurfaceController* controller_{nullptr};
};

class ControllerOperationResults final : public qs::SurfaceOperationResultSink {
 public:
  using Callback = std::function<void(qs::SurfaceOperationKind, qc::RequestId,
                                      std::optional<qc::SurfaceId>, bool,
                                      std::optional<qc::RuntimeError>)>;
  explicit ControllerOperationResults(Callback callback)
      : callback_(std::move(callback)) {}
  void complete(qs::SurfaceOperationKind kind, qc::RequestId request_id,
                std::optional<qc::SurfaceId> target, bool completed,
                std::optional<qc::RuntimeError> error) noexcept override {
    if (callback_) callback_(kind, std::move(request_id), std::move(target),
                             completed, std::move(error));
  }
  void close() noexcept override { callback_ = {}; }

 private:
  Callback callback_;
};

class PageLifecycle final : public qs::PageLifecyclePort {
 public:
  using Handler = std::function<qc::EnqueueResult(qs::PageCommand&&)>;
  explicit PageLifecycle(Handler handler) : handler_(std::move(handler)) {}
  qc::EnqueueResult post(qs::PageCommand&& command) noexcept override {
    if (!handler_) return qc::EnqueueResult::failure(
        qc::RuntimeError::simple(qc::RuntimeErrorCode::kPlatformRejected,
                                 "Android Page lifecycle is closed"));
    return handler_(std::move(command));
  }
  void close() noexcept override { handler_ = {}; }

 private:
  Handler handler_;
};

class InitialPipeline final : public qs::InitialSurfacePipeline {
 public:
  using Handler = std::function<qc::EnqueueResult(qs::InitialContentCommand&&)>;
  explicit InitialPipeline(Handler handler) : handler_(std::move(handler)) {}
  qc::EnqueueResult post(qs::InitialContentCommand&& command) noexcept override {
    if (!handler_) return qc::EnqueueResult::failure(
        qc::RuntimeError::simple(qc::RuntimeErrorCode::kPlatformRejected,
                                 "Android initial pipeline is closed"));
    return handler_(std::move(command));
  }
  void release_surface(const qc::SurfaceId&) noexcept override {}
  void close() noexcept override { handler_ = {}; }

 private:
  Handler handler_;
};

class MountResults final : public qc::CoreIngressPort<qr::MountTransactionResult> {
 public:
  void bind(qr::MountCoordinator& coordinator) noexcept { coordinator_ = &coordinator; }
  qc::EnqueueResult post(qr::MountTransactionResult&& result) noexcept override {
    return coordinator_ ? coordinator_->accept(std::move(result))
                        : qc::EnqueueResult::failure(platform::platformError(
                              "Android Mount coordinator is unavailable"));
  }
  void close() noexcept override {}

 private:
  qr::MountCoordinator* coordinator_{nullptr};
};

class RenderResults final : public qr::RenderTransactionResultSink {
 public:
  void bind(std::shared_ptr<ja::RuntimeAbiService> runtime_abi) noexcept {
    runtime_abi_ = std::move(runtime_abi);
  }
  void complete(qr::RenderTransactionResult result) noexcept override {
    if (!runtime_abi_) return;
    std::optional<ja::MessageRuntimeError> error;
    if (result.error) {
      error = ja::MessageRuntimeError{
          std::string(qc::to_wire(result.error->code)), result.error->message,
          result.error->retryable, result.surface_id.wire(), std::nullopt,
          result.transaction_id.wire(), std::nullopt};
    }
    static_cast<void>(runtime_abi_->postCallback(ja::JsInboundMessage{
        ja::RenderTransactionResult{result.surface_id.wire(),
                                     result.transaction_id.wire(),
                                     result.presented ? "presented"
                                                      : "presentationFailed",
                                     result.submitted_revision,
                                     result.committed_revision, std::move(error)}}));
  }
  void close() noexcept override { runtime_abi_.reset(); }

 private:
  std::shared_ptr<ja::RuntimeAbiService> runtime_abi_;
};

class JsCoreIngress final : public ja::CoreIngressPort,
                            public qc::event::JsEventDispatchPort {
 public:
  explicit JsCoreIngress(CoreMailbox& mailbox) : mailbox_(mailbox) {}

  void bind(qr::MountCoordinator& coordinator, qs::SurfaceController& controller,
            ja::RuntimeAbiService& runtime_abi) noexcept {
    coordinator_ = &coordinator;
    controller_ = &controller;
    runtime_abi_ = &runtime_abi;
  }
  void bindPage(const qc::SurfaceId& surface, qp::PageIrHandle page) {
    std::lock_guard lock(pages_mutex_);
    pages_[surface.wire()] = std::move(page);
  }

  qc::EnqueueResult post(qc::event::JsEventDispatch&& event) noexcept override {
    if (runtime_abi_ == nullptr) return qc::EnqueueResult::failure(
        qc::RuntimeError::simple(qc::RuntimeErrorCode::kPlatformRejected,
                                 "Android Runtime ABI is closed"));
    ja::JsEventDispatch dispatch{
        event.request_id.wire(), event.surface_id.wire(), event.handler_id.wire(),
        std::string(qc::event::event_type_wire(event.event_type)), event.phase,
        {qc::runtime_tree::owner_wire(event.target.owner),
         event.target.template_node_id.value()},
        {qc::runtime_tree::owner_wire(event.current_target.owner),
         event.current_target.template_node_id.value()},
        static_cast<double>(event.timestamp_ns), {}};
    const auto posted = runtime_abi_->postCallback(ja::JsInboundMessage{
        std::move(dispatch)});
    return posted.ok ? qc::EnqueueResult::success(qc::Accepted{})
                     : qc::EnqueueResult::failure(qc::RuntimeError::simple(
                           qc::RuntimeErrorCode::kQueueOverflow,
                           "Android JS event callback queue rejected"));
  }

  void close() noexcept override { runtime_abi_ = nullptr; }

  ja::EnqueueResult post(ja::CoreInboundMessage message) noexcept override {
    try {
      if (!mailbox_.post([this, message = std::move(message)]() mutable {
            handle(std::move(message));
          })) {
        return ja::EnqueueResult::rejected({ja::AbiErrorCode::QueueOverflow,
                                            "Android Core mailbox rejected message",
                                            true, std::nullopt, std::nullopt,
                                            std::nullopt, std::nullopt});
      }
      return ja::EnqueueResult::accepted();
    } catch (...) {
      return ja::EnqueueResult::rejected({ja::AbiErrorCode::OutOfMemory,
                                          "Android Core mailbox allocation failed",
                                          false, std::nullopt, std::nullopt,
                                          std::nullopt, std::nullopt});
    }
  }

 private:
  void handle(ja::CoreInboundMessage message) {
    if (coordinator_ == nullptr || controller_ == nullptr) return;
    if (auto* instantiate = std::get_if<ja::InstantiateTemplate>(&message)) {
      const auto surface = qc::SurfaceId::parse(instantiate->surfaceId);
      const auto owner = qc::ComponentInstanceId::parse(instantiate->ownerInstanceId);
      std::optional<qp::PageIrHandle> page;
      {
        std::lock_guard lock(pages_mutex_);
        auto found = pages_.find(instantiate->surfaceId);
        if (found != pages_.end()) page = found->second;
      }
      const auto request_id = qc::RequestId::parse(instantiate->requestId);
      if (!surface || !owner || !page || !request_id) return;
      std::map<std::uint64_t, qc::runtime_tree::BindingValue> bindings;
      for (const auto& [id, value] : instantiate->initialBindings) {
        bindings.emplace(id, std::visit([](const auto& item)
                                        -> qc::runtime_tree::BindingValue {
                                          return item;
                                        }, value));
      }
      std::vector<qc::runtime_tree::HandlerRegistration> handlers;
      for (const auto& value : instantiate->initialHandlers) {
        auto handler = qc::HandlerId::parse(value.handlerId);
        auto handler_owner = qc::ComponentInstanceId::parse(value.ownerInstanceId);
        auto template_id = qc::TemplateHandlerId::from(value.templateHandlerId);
        if (!handler || !handler_owner || !template_id) return;
        handlers.push_back({handler_owner.value(), template_id.value(), handler.value()});
      }
      static_cast<void>(coordinator_->submit(qr::InitialRenderIntent{
          surface.value(), request_id.value(), owner.value(), *page,
          std::move(bindings), {viewport_width_, viewport_height_},
          std::move(handlers)}));
      return;
    }
    if (auto* navigation = std::get_if<ja::NavigationPush>(&message)) {
      auto request_id = qc::RequestId::parse(navigation->requestId);
      auto source = qc::SurfaceId::parse(navigation->sourceSurfaceId);
      if (!request_id || !source) return;
      navigation_sources_[request_id.value().wire()] = source.value().wire();
      static_cast<void>(controller_->enqueue(qs::SurfaceRequest(
          qs::NavigationPushRequest{request_id.value(), source.value(), navigation->uri})));
      return;
    }
    if (auto* render = std::get_if<ja::SubmitRenderTransaction>(&message)) {
      auto surface = qc::SurfaceId::parse(render->surfaceId);
      auto transaction = qc::TransactionId::parse(render->transactionId);
      if (!surface || !transaction) return;
      std::vector<qc::runtime_tree::BindingUpdate> updates;
      for (const auto& operation : render->operations) {
        const auto* update = std::get_if<ja::UpdateBindingOperation>(&operation);
        if (!update) continue;
        auto owner = qc::ComponentInstanceId::parse(update->ownerInstanceId);
        if (!owner) continue;
        updates.push_back({owner.value(), update->templateBindingId,
                           std::visit([](const auto& item)
                                      -> qc::runtime_tree::BindingValue { return item; },
                                      update->value)});
      }
      std::optional<qc::RequestId> causal;
      if (render->requestId) causal = qc::RequestId::parse(*render->requestId).value();
      static_cast<void>(coordinator_->submit(qr::RenderTransactionIntent{
          surface.value(), transaction.value(), render->revision, causal,
          std::move(updates)}));
    }
  }

  CoreMailbox& mailbox_;
  qr::MountCoordinator* coordinator_{nullptr};
  qs::SurfaceController* controller_{nullptr};
  ja::RuntimeAbiService* runtime_abi_{nullptr};
  std::mutex pages_mutex_;
  std::map<std::string, qp::PageIrHandle, std::less<>> pages_;
  std::map<std::string, std::string, std::less<>> navigation_sources_;
  double viewport_width_{360};
  double viewport_height_{640};

 public:
  void setViewport(double width, double height) noexcept {
    viewport_width_ = width;
    viewport_height_ = height;
  }
  std::optional<std::string> takeNavigationSource(const std::string& request) {
    auto found = navigation_sources_.find(request);
    if (found == navigation_sources_.end()) return std::nullopt;
    auto value = std::move(found->second);
    navigation_sources_.erase(found);
    return value;
  }
};

}  // namespace

struct RuntimeSpine::Impl final {
  Impl(std::shared_ptr<platform::Gateway> gateway, double width, double height)
      : gateway(std::move(gateway)), mailbox(512), viewport_width(width),
        viewport_height(height) {}

  ~Impl() { destroy(); }

  void start(std::string path) noexcept {
    if (started.exchange(true)) return;
    core_thread = std::thread([this, path = std::move(path)]() mutable {
      try {
        run(std::move(path));
      } catch (const std::exception& error) {
        if (gateway) gateway->notifyFailed("RUNTIME_FAILED", error.what());
        running.store(false);
      } catch (...) {
        if (gateway) gateway->notifyFailed("RUNTIME_FAILED", "unknown runtime error");
        running.store(false);
      }
    });
  }

  void run(std::string path) {
    factory = std::make_unique<qc::AppRuntimeFactory>();
    identity = std::move(factory->create()).value();
    auto bytes = readFile(path);
    auto source = std::make_shared<MemorySource>(std::move(bytes));
    qp::RuntimeComposition composition{
        "quickapp-kit-runtime-v1", "quickapp-kit-js-engine-v1",
        {"View", "Text", "Button"},
        {"system.prompt", "system.router", "system.fetch", "system.device"}};
    loader = std::move(qp::PackageLoader::create(
                         source, identity.request_ids(), std::move(composition)))
                 .value();
    if (!loader->open([this](auto result) {
          if (result) package = std::move(result).value();
          else startup_error = result.error().message;
        }) || !package) {
      throw std::runtime_error(startup_error.empty() ? "RPK open failed" : startup_error);
    }

    auto provider = std::make_unique<qj::QuickJsEngineProvider>();
    Clock clock;
    TraceSink trace_sink;
    auto registration = qj::TraceSinkRegistration::admit(
        trace_sink, {.nonblocking = true, .noReentry = true});
    if (!registration) throw std::runtime_error("TraceSink registration failed");
    qj::JsEngineConfig engine_config;
    engine_config.expectedEngine = provider->describe();
    engine_config.limits.maxPendingTasks = 64;
    engine = std::make_unique<qj::JsEngineService>(
        identity->id().wire(), std::move(provider), engine_config, clock,
        std::move(registration).value(), {false, "run:android-a1", "android-monotonic", 0});
    std::promise<qj::ServiceResult> started_result;
    if (!engine->start([&](qj::ServiceResult result) {
          started_result.set_value(std::move(result));
        }) || !started_result.get_future().get()) {
      throw std::runtime_error("QuickJS start failed");
    }

    auto* surface_sink = gateway.get();
    (void)surface_sink;
    counters = std::make_unique<qc::RuntimeCounters>();
    mount_results = std::make_unique<MountResults>();
    auto measure = std::make_unique<platform::MeasurePort>();
    auto mount = std::make_unique<platform::MountPort>(*gateway);
    auto render_results = std::make_unique<RenderResults>();
    render_results_raw = render_results.get();
    auto initial_results = std::make_unique<ControllerInitialResults>();
    initial_results_raw = initial_results.get();
    core_ingress = std::make_unique<JsCoreIngress>(mailbox);
    core_ingress->setViewport(viewport_width, viewport_height);
    event_router = std::make_unique<qc::event::EventRouter>(*core_ingress);
    auto coordinator_result = qr::MountCoordinator::create(
        {&identity->request_ids(), counters.get(), std::move(measure),
         std::move(mount), std::move(initial_results), nullptr, nullptr,
         event_router.get(), std::move(render_results)});
    if (!coordinator_result) throw std::runtime_error("MountCoordinator create failed");
    coordinator = std::move(coordinator_result).value();
    mount_results->bind(*coordinator);

    facades = std::make_unique<qj::framework::StaticFacadeCatalog>();
    module_completion = std::make_unique<ModuleCompletion>();
    modules = nullptr;
    runtime_abi = nullptr;
    handler_registry = nullptr;
    page_controls = nullptr;
    binding_stage = nullptr;
    transaction_builder = nullptr;
    page_stage = nullptr;
    vm = nullptr;

    auto platform_port = std::make_unique<platform::SurfacePort>(*gateway);
    auto page_lifecycle = std::make_unique<PageLifecycle>(
        [this](qs::PageCommand&& command) { return postPageCommand(std::move(command)); });
    auto initial_pipeline = std::make_unique<InitialPipeline>(
        [this](qs::InitialContentCommand&& command) {
          return postInitialCommand(std::move(command));
        });
    auto pages = std::make_unique<PageResolver>(*loader, package);
    auto operations = std::make_unique<ControllerOperationResults>(
        [this](qs::SurfaceOperationKind kind, qc::RequestId request,
               std::optional<qc::SurfaceId> target, bool completed,
               std::optional<qc::RuntimeError> error) {
          onSurfaceOperation(kind, std::move(request), std::move(target),
                             completed, std::move(error));
        });
    auto controller_result = qs::SurfaceController::create(
        {nullptr, &identity->request_ids(), std::move(pages),
         std::move(platform_port), std::move(page_lifecycle),
         std::move(initial_pipeline), std::move(operations),
         std::make_unique<ControllerStatus>(),
         std::make_unique<ControllerLifecycleResults>(), counters.get()});
    if (!controller_result) throw std::runtime_error("SurfaceController create failed");
    controller = std::move(controller_result).value();

    setupJs();
    core_ingress->bind(*coordinator, *controller, *runtime_abi);
    if (!postRoot()) throw std::runtime_error("Android root request rejected");
    running.store(true);
    while (!stopping.load()) {
      mailbox.drain(128);
      if (controller) static_cast<void>(controller->drain());
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (mailbox.depth() == 0 && controller && !controller->snapshot().accepting &&
          stopping.load()) break;
    }
    cleanup();
  }

  bool postRoot() {
    return controller->enqueue(qs::SurfaceRequest(qs::RootSurfaceRequest{
        parseRequest("req:android-root"), package->entry_route()}));
  }

  void setupJs() {
    js_request_ids = std::make_unique<RequestIds>();
    runtime_abi = std::make_shared<ja::RuntimeAbiService>(
        *engine, *core_ingress, ja::RuntimeAbiLimits{},
        ja::CapabilitySupportSnapshot{});
    const auto setup = engine->post([this](qj::JsEnginePort& js,
                                           const qj::JsContextRef& context) {
      if (!facades->startOnExecutor(js, context)) throw std::runtime_error("facade setup failed");
      modules = new qj::module::ModuleLoader(
          *engine, *module_completion, identity->id().wire(), package->package_id(),
          qj::module::ModuleLoaderLimits{}, facades.get());
      if (!modules->startOnExecutor(js, context)) throw std::runtime_error("module setup failed");
      if (!runtime_abi->startOnExecutor(js, context, ja::kRuntimeAbiIdentity))
        throw std::runtime_error("ABI setup failed");
      handler_registry = new qj::event::HandlerRegistry(*engine);
      page_controls = new qj::page::PageHostControlInstaller(*engine, *runtime_abi, *js_request_ids);
      binding_stage = new qj::binding::AlphaInitialBindingStage(*engine, *modules);
      transaction_builder = new qj::render::AlphaInitialTransactionBuilder(*engine, *js_request_ids);
      if (!handler_registry->startOnExecutor(js, context) ||
          !page_controls->startOnExecutor(js, context) ||
          !binding_stage->startOnExecutor(js, context) ||
          !transaction_builder->startOnExecutor(js, context))
        throw std::runtime_error("JS framework setup failed");
      page_stage = new qj::alpha::AlphaPageInitializationStage(*binding_stage, *transaction_builder);
      vm = new qj::vm::VmLifecycleService(*engine, *modules, *page_controls, *page_stage,
                                          package->package_id());
      auto slots = modules->callbackSlots();
      auto vm_slots = vm->callbackSlots();
      slots.appContext = std::move(vm_slots.appContext);
      slots.surfaceContext = std::move(vm_slots.surfaceContext);
      slots.vmInitializationDispatch = std::move(vm_slots.vmInitializationDispatch);
      slots.jsEventDispatch = [this](const ja::JsEventDispatch& event) {
        if (handler_registry) static_cast<void>(handler_registry->dispatchOnExecutor(event));
      };
      slots.renderTransactionResult = [](const ja::RenderTransactionResult&) {};
      if (!runtime_abi->registerConsumersOnExecutor(std::move(slots)) ||
          !vm->startOnExecutor(js, context))
        throw std::runtime_error("JS consumer registration failed");

      std::uint64_t sequence = 1;
      auto load = [&](const qp::VerifiedModule& module, std::string kind,
                      std::string scope, std::optional<ja::BootstrapExpectation> bootstrap) {
        ja::LoadVerifiedModule message;
        message.requestId = "req:android-module-" + std::to_string(sequence++);
        message.packageId = module.package_id();
        message.moduleKind = std::move(kind);
        message.moduleId = module.module_id();
        message.cacheScope = std::move(scope);
        message.dependencies = module.dependencies();
        message.bundle = {module.descriptor().path, module.descriptor().byte_length,
                          module.descriptor().sha256,
                          std::make_shared<const std::vector<std::uint8_t>>(*module.bytes())};
        message.expectedBootstrap = std::move(bootstrap);
        message.expectedBindingIds = module.expected_binding_ids();
        message.expectedHandlerIds = module.expected_handler_ids();
        modules->onLoadVerifiedModule(message);
      };
      for (const auto& [module_id, descriptor] : package->modules()) {
        if (descriptor.kind == qp::ModuleKind::kShared) {
          std::optional<qp::VerifiedModule> module;
          if (!loader->load_module({module_id, std::nullopt}, [&](auto result) {
                if (result) module = std::move(result).value();
              }) || !module) throw std::runtime_error("shared module load failed");
          load(*module, "shared", "appRuntime", std::nullopt);
        }
      }
      std::optional<qp::VerifiedModule> app;
      if (!loader->load_module({"@quickapp-kit/app", std::nullopt}, [&](auto result) {
            if (result) app = std::move(result).value();
          }) || !app) throw std::runtime_error("app module load failed");
      load(*app, "app", "appRuntime",
           ja::BootstrapExpectation{"app", app->module_id(), std::nullopt});
      vm->onAppContext({package->package_id(), "1.0.0", "1", 1,
                        {"system.router", "system.prompt", "system.device"}});
      vm->onVmInitialization({parseRequest("req:android-app-init"), "app", std::nullopt});
    });
    if (setup.status != qj::PostStatus::Accepted) throw std::runtime_error("JS setup enqueue failed");
    waitForJsSetup();
  }

  void waitForJsSetup() {
    for (std::size_t i = 0; i < 5000; ++i) {
      if (modules != nullptr && vm != nullptr && runtime_abi != nullptr &&
          runtime_abi->state() == ja::RuntimeAbiServiceState::Running) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("JS setup timeout");
  }

  qc::EnqueueResult postPageCommand(qs::PageCommand&& command) {
    if (!engine || !modules || !vm || !runtime_abi) return qc::EnqueueResult::failure(
        qc::RuntimeError::simple(qc::RuntimeErrorCode::kPlatformRejected, "JS page service unavailable"));
    auto task = engine->post([this, command = std::move(command)](
                                 qj::JsEnginePort&, const qj::JsContextRef&) mutable {
      auto complete = [this](const qs::PageCommand& value, bool ok) {
        const auto* start = std::get_if<qs::PageStartCommand>(&value);
        const auto* hook = std::get_if<qs::PageHookCommand>(&value);
        static_cast<void>(controller->enqueue(qs::PageLifecycleResult{
            start ? start->request_id : hook->request_id,
            start ? qs::PageCommandKind::kStart : qs::PageCommandKind::kHook,
            start ? start->surface_id : hook->surface_id,
            hook ? std::optional<qs::PageHook>(hook->hook) : std::nullopt,
            ok, std::nullopt}));
      };
      if (auto* start = std::get_if<qs::PageStartCommand>(&command)) {
        if (!runtime_abi->openSurfaceOnExecutor(start->surface_id.wire()) ||
            !modules->openSurfaceOnExecutor(start->surface_id.wire())) {
          complete(command, false);
          return;
        }
        const auto& module = start->page.module;
        modules->onLoadVerifiedModule(ja::LoadVerifiedModule{
            "req:android-page-load", module.package_id(), "page", module.module_id(),
            "surface", start->surface_id.wire(),
            {module.descriptor().path, module.descriptor().byte_length,
             module.descriptor().sha256,
             std::make_shared<const std::vector<std::uint8_t>>(*module.bytes())},
            module.dependencies(),
            ja::BootstrapExpectation{"page", module.module_id(), module.expected_template_id()},
            module.expected_binding_ids(), module.expected_handler_ids()});
        core_ingress->bindPage(start->surface_id, start->page.page_ir);
        vm->onSurfaceContext({start->surface_id.wire(), package->package_id(),
                              start->page.route, module.expected_template_id().value_or(""),
                              {}, {"setTitleBar", "setMeta"},
                              {viewport_width, viewport_height, "logical-px"}});
        vm->onVmInitialization({parseRequest("req:android-page-init"), "page",
                                start->surface_id});
        complete(command, true);
      } else if (auto* hook = std::get_if<qs::PageHookCommand>(&command)) {
        if (hook->hook == qs::PageHook::kOnDestroy) {
          if (handler_registry) handler_registry->closeSurface(hook->surface_id.wire());
          event_router->closeSurface(hook->surface_id);
          vm->closeSurfaceOnExecutor(hook->surface_id.wire());
        }
        complete(command, true);
      }
    });
    return task.status == qj::PostStatus::Accepted
               ? qc::EnqueueResult::success(qc::Accepted{})
               : qc::EnqueueResult::failure(qc::RuntimeError::simple(
                     qc::RuntimeErrorCode::kQueueOverflow, "Android JS queue rejected"));
  }

  qc::EnqueueResult postInitialCommand(qs::InitialContentCommand&& command) {
    if (!coordinator || !engine) return qc::EnqueueResult::failure(platform::platformError("Android initial services unavailable"));
    const auto surface = command.surface_id;
    const auto page_ir = command.page_ir;
    auto posted = coordinator->post(std::move(command));
    if (!posted) return posted;
    auto task = engine->post([this, surface, page_ir](qj::JsEnginePort&, const qj::JsContextRef&) {
      if (!modules || !handler_registry) return;
      const auto definition = modules->pageDefinitionForSurfaceOnExecutor(
          surface.wire(), page_ir->template_id());
      if (!definition) return;
      const auto handlers = modules->handlerBindingsOnExecutor(
          *definition, "cmp:" + surface.wire());
      if (!handlers) return;
      for (const auto& binding : handlers.value()) {
        const auto method = modules->handlerMethodNameOnExecutor(
            *definition, binding.templateHandlerId);
        auto vm_value = vm->pageVmOnExecutor(surface.wire());
        if (method && vm_value) static_cast<void>(handler_registry->bind(
            surface.wire(), binding.handlerId, *method, std::move(vm_value).value()));
      }
    });
    return task.status == qj::PostStatus::Accepted
               ? qc::EnqueueResult::success(qc::Accepted{})
               : qc::EnqueueResult::failure(qc::RuntimeError::simple(
                     qc::RuntimeErrorCode::kQueueOverflow, "Android initial JS queue rejected"));
  }

  void onSurfaceOperation(qs::SurfaceOperationKind kind, qc::RequestId request,
                          std::optional<qc::SurfaceId> target, bool completed,
                          std::optional<qc::RuntimeError> error) {
    if (kind != qs::SurfaceOperationKind::kPush || !runtime_abi) return;
    auto source = core_ingress->takeNavigationSource(request.wire());
    if (!source) return;
    std::optional<ja::MessageRuntimeError> mapped;
    if (error) mapped = ja::MessageRuntimeError{
        std::string(qc::to_wire(error->code)), error->message, error->retryable,
        std::nullopt, request.wire(), std::nullopt, std::nullopt};
    static_cast<void>(runtime_abi->postCallback(ja::JsInboundMessage{
        ja::NavigationPushResult{request.wire(), *source,
                                  completed ? "completed" : "failed",
                                  target ? std::optional<std::string>(target->wire())
                                         : std::nullopt,
                                  std::move(mapped)}}));
  }

  void acceptSurfaceResult(std::string request_id, int kind,
                           std::string target, std::optional<std::string> source,
                           std::optional<std::string> reveal, int visibility,
                           bool completed, std::optional<std::string> code,
                           std::optional<std::string> message) noexcept {
    mailbox.post([this, request_id = std::move(request_id), kind,
                  target = std::move(target), source = std::move(source),
                  reveal = std::move(reveal), visibility, completed,
                  code = std::move(code), message = std::move(message)]() mutable {
      auto request = qc::RequestId::parse(request_id);
      auto target_id = qc::SurfaceId::parse(target);
      if (!request || !target_id || !controller) return;
      std::optional<qc::RuntimeError> error;
      if (!completed) error = platform::platformError(message.value_or("Android Surface operation failed"));
      if (kind == 0) static_cast<void>(controller->enqueue(qs::SurfaceCommandResult{
          request.value(), qs::SurfaceCommandKind::kCreate, target_id.value(),
          std::nullopt, std::nullopt, std::nullopt, completed, std::move(error)}));
      else if (kind == 1) {
        auto source_id = source ? qc::SurfaceId::parse(*source) : std::nullopt;
        static_cast<void>(controller->enqueue(qs::SurfaceCommandResult{
            request.value(), qs::SurfaceCommandKind::kPresent, target_id.value(),
            source_id, std::nullopt, std::nullopt, completed, std::move(error)}));
      } else if (kind == 2) static_cast<void>(controller->enqueue(qs::SurfaceCommandResult{
          request.value(), qs::SurfaceCommandKind::kVisibility, target_id.value(),
          std::nullopt, std::nullopt,
          visibility == 1 ? std::optional<qc::lifecycle::SurfaceVisibility>(qc::lifecycle::SurfaceVisibility::kVisible)
                          : std::optional<qc::lifecycle::SurfaceVisibility>(qc::lifecycle::SurfaceVisibility::kHidden),
          completed, std::move(error)}));
      else if (kind == 3) {
        auto source_id = source ? qc::SurfaceId::parse(*source) : std::nullopt;
        auto reveal_id = reveal ? qc::SurfaceId::parse(*reveal) : std::nullopt;
        static_cast<void>(controller->enqueue(qs::SurfaceCommandResult{
            request.value(), qs::SurfaceCommandKind::kClose, target_id.value(),
            source_id, reveal_id, std::nullopt, completed, std::move(error)}));
      } else static_cast<void>(controller->enqueue(qs::SurfaceCommandResult{
          request.value(), qs::SurfaceCommandKind::kDestroy, target_id.value(),
          std::nullopt, std::nullopt, std::nullopt, completed, std::move(error)}));
    });
  }

  void acceptMountResult(std::string surface_id, std::uint64_t revision,
                         std::string attempt, std::string source, bool mounted,
                         std::optional<std::string> code,
                         std::optional<std::string> message) noexcept {
    mailbox.post([this, surface_id = std::move(surface_id), revision,
                  attempt = std::move(attempt), source = std::move(source), mounted,
                  code = std::move(code), message = std::move(message)]() mutable {
      auto surface = qc::SurfaceId::parse(surface_id);
      auto mount_attempt = qc::MountAttemptId::parse(attempt);
      if (!surface || !mount_attempt || !coordinator) return;
      qr::RenderSourceId source_id = source.starts_with("txn:")
                                         ? qr::RenderSourceId(qc::TransactionId::parse(source).value())
                                         : qr::RenderSourceId(qc::RequestId::parse(source).value());
      static_cast<void>(coordinator->accept(qr::MountTransactionResult{
          surface.value(), revision, mount_attempt.value(), source_id, mounted,
          mounted ? std::nullopt
                  : std::optional<qc::RuntimeError>(platform::platformError(
                        message.value_or("Android Mount failed")))}));
    });
  }

  void dispatchClick(std::string surface_id, std::string node_id,
                     std::uint64_t timestamp_ns) noexcept {
    mailbox.post([this, surface_id = std::move(surface_id), node_id = std::move(node_id),
                  timestamp_ns]() mutable {
      auto surface = qc::SurfaceId::parse(surface_id);
      auto node = qc::NodeId::parse(node_id);
      if (!surface || !node || !event_router) return;
      auto request = identity->request_ids().next();
      if (!request) return;
      static_cast<void>(event_router->dispatch(qc::event::PlatformInputMessage{
          request.value(), surface.value(), node.value(), qp::EventType::kClick,
          timestamp_ns, {}}));
    });
  }

  void destroy() noexcept {
    if (stopping.exchange(true)) return;
    mailbox.close();
    if (core_thread.joinable()) core_thread.join();
  }

  void cleanup() noexcept {
    if (controller) {
      controller->force_teardown();
      controller.reset();
    }
    if (coordinator) {
      coordinator->close();
      coordinator.reset();
    }
    if (engine) {
      std::promise<void> stopped_result;
      engine->post([this](qj::JsEnginePort&, const qj::JsContextRef&) {
        if (handler_registry) handler_registry->stopOnExecutor();
        if (vm) vm->stopOnExecutor();
        if (transaction_builder) transaction_builder->stopOnExecutor();
        if (binding_stage) binding_stage->stopOnExecutor();
        if (page_controls) page_controls->stopOnExecutor();
        if (runtime_abi) runtime_abi->stopOnExecutor();
        if (modules) modules->stopOnExecutor();
        if (facades) facades->stopOnExecutor();
      });
      engine->stop({}, [&] { stopped_result.set_value(); });
      stopped_result.get_future().wait();
    }
    delete handler_registry;
    delete vm;
    delete page_stage;
    delete transaction_builder;
    delete binding_stage;
    delete page_controls;
    runtime_abi.reset();
    delete modules;
    modules = nullptr;
    facades.reset();
    loader.reset();
    if (factory) {
      factory->stop();
      if (identity) identity->reset();
      factory->teardown();
      identity.reset();
      factory.reset();
    }
    if (gateway) {
      gateway->notifyStopped(0, 0, 0, 0, 0, mailbox.depth());
      gateway->close();
    }
  }

  std::shared_ptr<platform::Gateway> gateway;
  CoreMailbox mailbox;
  const double viewport_width;
  const double viewport_height;
  std::atomic<bool> started{false};
  std::atomic<bool> stopping{false};
  std::atomic<bool> running{false};
  std::thread core_thread;
  std::unique_ptr<qc::AppRuntimeFactory> factory;
  std::optional<qc::AppRuntimeIdentity> identity;
  std::shared_ptr<qp::PackageLoader> loader;
  std::shared_ptr<const qp::VerifiedPackage> package;
  std::string startup_error;
  std::unique_ptr<qj::JsEngineService> engine;
  std::unique_ptr<qc::RuntimeCounters> counters;
  std::unique_ptr<MountResults> mount_results;
  std::unique_ptr<ControllerInitialResults> initial_results_raw;
  RenderResults* render_results_raw{nullptr};
  std::unique_ptr<qr::MountCoordinator> coordinator;
  std::unique_ptr<qc::event::EventRouter> event_router;
  std::unique_ptr<JsCoreIngress> core_ingress;
  std::unique_ptr<qj::framework::StaticFacadeCatalog> facades;
  std::unique_ptr<ModuleCompletion> module_completion;
  std::unique_ptr<qj::module::ModuleLoader> modules;
  std::shared_ptr<ja::RuntimeAbiService> runtime_abi;
  qj::event::HandlerRegistry* handler_registry{nullptr};
  qj::page::PageHostControlInstaller* page_controls{nullptr};
  qj::binding::AlphaInitialBindingStage* binding_stage{nullptr};
  qj::render::AlphaInitialTransactionBuilder* transaction_builder{nullptr};
  qj::alpha::AlphaPageInitializationStage* page_stage{nullptr};
  qj::vm::VmLifecycleService* vm{nullptr};
  std::unique_ptr<qs::SurfaceController> controller;
  std::unique_ptr<RequestIds> js_request_ids;
};

RuntimeSpine::RuntimeSpine(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

RuntimeSpine::~RuntimeSpine() { destroy(); }

std::shared_ptr<RuntimeSpine> RuntimeSpine::create(
    std::shared_ptr<platform::Gateway> gateway, double width,
    double height) noexcept {
  try {
    return std::shared_ptr<RuntimeSpine>(
        new RuntimeSpine(std::make_unique<Impl>(std::move(gateway), width, height)));
  } catch (...) {
    return nullptr;
  }
}

void RuntimeSpine::start(std::string path) noexcept {
  if (impl_) impl_->start(std::move(path));
}

void RuntimeSpine::dispatchClick(std::string surface_id, std::string node_id,
                                 std::uint64_t timestamp_ns) noexcept {
  if (impl_) impl_->dispatchClick(std::move(surface_id), std::move(node_id), timestamp_ns);
}

void RuntimeSpine::acceptSurfaceResult(
    std::string request_id, int kind, std::string target,
    std::optional<std::string> source, std::optional<std::string> reveal,
    int visibility, bool completed, std::optional<std::string> code,
    std::optional<std::string> message) noexcept {
  if (impl_) impl_->acceptSurfaceResult(std::move(request_id), kind,
                                        std::move(target), std::move(source),
                                        std::move(reveal), visibility, completed,
                                        std::move(code), std::move(message));
}

void RuntimeSpine::acceptMountResult(
    std::string surface_id, std::uint64_t revision, std::string attempt,
    std::string source, bool mounted, std::optional<std::string> code,
    std::optional<std::string> message) noexcept {
  if (impl_) impl_->acceptMountResult(std::move(surface_id), revision,
                                      std::move(attempt), std::move(source), mounted,
                                      std::move(code), std::move(message));
}

void RuntimeSpine::destroy() noexcept {
  if (impl_) impl_->destroy();
}

RuntimeSpineSnapshot RuntimeSpine::snapshot() const noexcept {
  if (!impl_) return {};
  RuntimeSpineSnapshot result;
  result.pending_callbacks = impl_->gateway ? impl_->gateway->pendingCallbacks() : 0;
  result.core_queue_depth = impl_->mailbox.depth();
  if (impl_->controller) {
    const auto snapshot = impl_->controller->snapshot();
    result.surfaces = snapshot.records.size();
    result.core_queue_depth += snapshot.pending_correlations;
  }
  result.handlers = impl_->event_router ? impl_->event_router->handlerCount() : 0;
  if (impl_->coordinator) {
    result.nodes = impl_->coordinator->snapshot().committed_nodes;
  }
  if (impl_->vm) {
    const auto resources = impl_->vm->resources();
    result.js_resources = resources.appVms + resources.pageVms + resources.openSurfaces;
  }
  return result;
}

}  // namespace quickapp::android
