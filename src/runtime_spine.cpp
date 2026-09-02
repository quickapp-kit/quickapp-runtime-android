#include "quickapp/android/runtime_spine.h"

#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "quickapp/core/foundation/app_runtime_factory.h"
#include "quickapp/core/feature/module_registry.h"
#include "quickapp/core/package/package_loader.h"
#include "quickapp/core/render/initial_render_pipeline.h"
#include "quickapp/core/surface/surface_controller.h"
#include "quickapp/android/feature_provider.h"
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

void androidStage(const char* stage) noexcept {
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "QuickAppKit", "android.stage=%s", stage);
#else
  (void)stage;
#endif
}

namespace qc = core;
namespace qp = core::package;
namespace qr = core::render;
namespace qs = core::surface;
namespace qj = js;
namespace ja = js::abi;
namespace qcf = core::feature;

std::optional<qj::RuntimeValue> toJsRuntimeValue(
    const qc::RuntimeValue& value) {
  const auto& storage = value.storage();
  if (std::holds_alternative<qc::RuntimeValue::Null>(storage)) {
    return qj::RuntimeValue(nullptr);
  }
  if (const auto* boolean = std::get_if<bool>(&storage)) {
    return qj::RuntimeValue(*boolean);
  }
  if (const auto* integer = std::get_if<std::int64_t>(&storage)) {
    return qj::RuntimeValue(static_cast<double>(*integer));
  }
  if (const auto* number = std::get_if<double>(&storage)) {
    return qj::RuntimeValue(*number);
  }
  if (const auto* string = std::get_if<std::string>(&storage)) {
    return qj::RuntimeValue(*string);
  }
  if (const auto* array =
          std::get_if<std::shared_ptr<const qc::RuntimeValue::Array>>(&storage)) {
    if (!*array) return qj::RuntimeValue(nullptr);
    qj::RuntimeValue::Array converted;
    converted.reserve((*array)->size());
    for (const auto& item : **array) {
      auto value = toJsRuntimeValue(item);
      if (!value) return std::nullopt;
      converted.emplace_back(std::move(*value));
    }
    return qj::RuntimeValue(std::move(converted));
  }
  if (const auto* object =
          std::get_if<std::shared_ptr<const qc::RuntimeValue::Object>>(&storage)) {
    if (!*object) return qj::RuntimeValue(nullptr);
    qj::RuntimeValue::Object converted;
    for (const auto& [name, item] : **object) {
      auto value = toJsRuntimeValue(item);
      if (!value) return std::nullopt;
      converted.emplace(name, std::move(*value));
    }
    return qj::RuntimeValue(std::move(converted));
  }
  return std::nullopt;
}

std::optional<ja::DynamicValues> toJsDynamicValues(
    const qc::RuntimeValue::Object& payload) {
  ja::DynamicValues converted;
  for (const auto& [name, value] : payload) {
    auto item = toJsRuntimeValue(value);
    if (!item) return std::nullopt;
    converted.emplace(name, std::move(*item));
  }
  return converted;
}

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
      const qj::module::ModuleLoadCompletion& completion) noexcept override {
    androidStage(completion.status == "loaded" ? "js.module.loaded"
                                                : "js.module.failed");
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                        "android.js.module id=%s kind=%s status=%s error=%s",
                        completion.moduleId.c_str(), completion.moduleKind.c_str(),
                        completion.status.c_str(),
                        completion.error ? completion.error->message.c_str() : "");
#endif
    return {qj::module::ModuleEnqueueStatus::Accepted};
  }
};

class RequestIds final : public qj::framework::JsRequestIdAllocatorPort {
 public:
  std::string nextRequestId() noexcept override {
    return "req:j-" + std::to_string(next_++);
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
  explicit ControllerInitialResults(
      std::unique_ptr<qs::SurfaceController>* controller_slot) noexcept
      : controller_slot_(controller_slot) {}

  void complete(qs::InitialContentResult result) noexcept override {
    androidStage(result.prepared ? "core.initial.prepared"
                                 : "core.initial.failed");
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                        "android.initial.result surface=%s prepared=%d error=%s",
                        result.surface_id.wire().c_str(), result.prepared ? 1 : 0,
                        result.error ? std::string(result.error->message).c_str() : "");
#endif
    auto* controller = controller_slot_ == nullptr ? nullptr : controller_slot_->get();
    const auto accepted = controller ? controller->enqueue(std::move(result))
                                      : qc::EnqueueResult::failure(
                                            qc::RuntimeError::simple(
                                                qc::RuntimeErrorCode::kPlatformRejected,
                                                "Android controller unavailable"));
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                        "android.initial.controller.enqueue=%d", accepted ? 1 : 0);
#endif
  }
  void close() noexcept override {}

 private:
  std::unique_ptr<qs::SurfaceController>* controller_slot_{nullptr};
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
          std::string(qc::to_wire(result.error->code)),
          std::string(result.error->message),
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
  using HandlerBindingSink =
      std::function<void(std::string, std::string,
                         std::vector<ja::HandlerBinding>)>;
  using HandlerUnbindSink =
      std::function<void(std::string, std::vector<std::string>)>;

  explicit JsCoreIngress(CoreMailbox& mailbox) : mailbox_(mailbox) {}

  void bindEventRouter(qc::event::EventRouter& event_router) noexcept {
    event_router_ = &event_router;
  }

  void bind(qr::MountCoordinator& coordinator, qs::SurfaceController& controller,
            ja::RuntimeAbiService& runtime_abi,
            qcf::ModuleRegistry& feature_registry) noexcept {
    coordinator_ = &coordinator;
    controller_ = &controller;
    runtime_abi_ = &runtime_abi;
    feature_registry_ = &feature_registry;
  }
  void bindHandlerSinks(HandlerBindingSink bind_sink,
                        HandlerUnbindSink unbind_sink) noexcept {
    handler_binding_sink_ = std::move(bind_sink);
    handler_unbind_sink_ = std::move(unbind_sink);
  }
  void bindPage(const qc::SurfaceId& surface, qp::PageIrHandle page) {
    std::lock_guard lock(pages_mutex_);
    pages_[surface.wire()] = std::move(page);
  }

  qc::EnqueueResult post(qc::event::JsEventDispatch&& event) noexcept override {
    if (runtime_abi_ == nullptr) return qc::EnqueueResult::failure(
        qc::RuntimeError::simple(qc::RuntimeErrorCode::kPlatformRejected,
                                 "Android Runtime ABI is closed"));
    auto payload = toJsDynamicValues(event.payload);
    if (!payload) return qc::EnqueueResult::failure(
        qc::RuntimeError::simple(qc::RuntimeErrorCode::kAbiInvalidArgument,
                                 "Android event payload conversion failed"));
#if defined(__ANDROID__)
    const auto valueType = [](const qj::RuntimeValue* value) {
      if (value == nullptr) return "absent";
      const auto& storage = value->storage();
      if (std::holds_alternative<std::nullptr_t>(storage)) return "null";
      if (std::holds_alternative<bool>(storage)) return "boolean";
      if (std::holds_alternative<double>(storage)) return "number";
      if (std::holds_alternative<std::string>(storage)) return "string";
      if (std::holds_alternative<qj::RuntimeValue::Array>(storage)) return "array";
      return "object";
    };
    const auto field = [&](std::string_view name) -> const qj::RuntimeValue* {
      const auto found = payload->find(std::string(name));
      return found == payload->end() ? nullptr : &found->second;
    };
    __android_log_print(
        ANDROID_LOG_INFO, "QuickAppKit",
        "android.event.payload event=%s keys=%zu index=%s value=%s checked=%s "
        "isFromUser=%s selected=%s scrollOffset=%s",
        event.event_type == qc::package::EventType::kClick
            ? "click"
            : std::string(qc::event::event_type_wire(event.event_type)).c_str(),
        payload->size(), valueType(field("index")), valueType(field("value")),
        valueType(field("checked")), valueType(field("isFromUser")),
        valueType(field("selected")), valueType(field("scrollOffset")));
#endif
    ja::JsEventDispatch dispatch{
        event.request_id.wire(), event.surface_id.wire(), event.handler_id.wire(),
        std::string(qc::event::event_type_wire(event.event_type)), event.phase,
        {qc::runtime_tree::owner_wire(event.target.owner),
         event.target.template_node_id.value()},
        {qc::runtime_tree::owner_wire(event.current_target.owner),
         event.current_target.template_node_id.value()},
        static_cast<double>(event.timestamp_ns), std::move(*payload)};
    const auto posted = runtime_abi_->postCallback(ja::JsInboundMessage{
        std::move(dispatch)});
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                        "android.event.js_callback posted=%d error=%s",
                        posted.ok ? 1 : 0,
                        posted.error ? std::string(ja::abiErrorCodeName(
                            posted.error->code)).c_str() : "");
#endif
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
      androidStage("core.ingress.instantiate");
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
        const auto* definition = (*page)->find_handler(template_id.value().value());
        if (definition == nullptr) return;
        // JS reports the complete page handler catalog. Block-scoped handlers
        // are carried by their InitialBlock request and must not be staged as
        // page handlers, or Core will reject the initial transaction.
        if (definition->scope_block_id.has_value()) continue;
        handlers.push_back({handler_owner.value(), template_id.value(), handler.value()});
      }

      const auto parse_owner = [](const std::string& wire)
          -> std::optional<qc::OwnerInstanceId> {
        if (const auto component = qc::ComponentInstanceId::parse(wire)) {
          return qc::OwnerInstanceId(component.value());
        }
        if (const auto block = qc::BlockInstanceId::parse(wire)) {
          return qc::OwnerInstanceId(block.value());
        }
        return std::nullopt;
      };
      std::vector<qc::runtime_tree::InstantiateBlockRequest> initial_blocks;
      std::vector<ja::HandlerBinding> js_block_handlers;
      initial_blocks.reserve(instantiate->initialBlocks.size());
      for (const auto& block : instantiate->initialBlocks) {
        const auto block_id = qc::BlockInstanceId::parse(block.blockInstanceId);
        const auto template_id = qc::TemplateBlockId::from(block.templateBlockId);
        const auto parent_template_id =
            qc::TemplateNodeId::from(block.parent.templateNodeId);
        const auto parent_owner = parse_owner(block.parent.ownerInstanceId);
        if (!block_id || !template_id || !parent_template_id || !parent_owner) return;

        std::map<std::uint64_t, qc::runtime_tree::BindingValue> block_bindings;
        for (const auto& [id, value] : block.initialBindings) {
          block_bindings.emplace(id, std::visit(
              [](const auto& item) -> qc::runtime_tree::BindingValue {
                return item;
              }, value));
        }
        std::vector<qc::runtime_tree::HandlerRegistration> block_handlers;
        for (const auto& binding : block.handlers) {
          const auto owner = qc::BlockInstanceId::parse(binding.ownerInstanceId);
          const auto handler_template =
              qc::TemplateHandlerId::from(binding.templateHandlerId);
          const auto handler_id = qc::HandlerId::parse(binding.handlerId);
          if (!owner || !handler_template || !handler_id) return;
          block_handlers.push_back(
              {owner.value(), handler_template.value(), handler_id.value()});
          js_block_handlers.push_back(binding);
          block_handler_ids_[block_id.value().wire()].push_back(binding.handlerId);
        }
        qc::runtime_tree::BlockKey key = std::string("");
        if (block.key.has_value()) {
          key = std::visit([](const auto& item) -> qc::runtime_tree::BlockKey {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, std::string>) {
              return item;
            } else {
              return static_cast<std::int64_t>(item);
            }
          }, *block.key);
        }
        initial_blocks.push_back({
            template_id.value(), block_id.value(),
            {parent_owner.value(), parent_template_id.value()}, block.index,
            std::move(key), std::move(block_bindings), std::move(block_handlers)});
      }
      if (handler_binding_sink_ && !js_block_handlers.empty()) {
        handler_binding_sink_(surface.value().wire(), instantiate->templateId,
                             std::move(js_block_handlers));
      }
      const auto submitted = coordinator_->submit(qr::InitialRenderIntent{
          surface.value(), request_id.value(), owner.value(), *page,
          std::move(bindings), {viewport_width_, viewport_height_},
          std::move(handlers), std::move(initial_blocks)});
      androidStage(submitted ? "core.ingress.instantiate.accepted"
                             : "core.ingress.instantiate.rejected");
#if defined(__ANDROID__)
      const auto handler = qc::HandlerId::parse("hdl:1");
      const auto node = handler && event_router_
                            ? event_router_->nodeForHandler(surface.value(), handler.value())
                            : std::nullopt;
      __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                          "android.event.handlers count=%zu hdl1node=%s",
                          event_router_ ? event_router_->handlerCount() : 0,
                          node ? node->wire().c_str() : "");
#endif
      return;
    }
    if (auto* navigation = std::get_if<ja::NavigationPush>(&message)) {
      auto request_id = qc::RequestId::parse(navigation->requestId);
      auto source = qc::SurfaceId::parse(navigation->sourceSurfaceId);
      if (!request_id || !source) return;
      navigation_sources_[request_id.value().wire()] = source.value().wire();
      const auto accepted = controller_->enqueue(qs::SurfaceRequest(
          qs::NavigationPushRequest{request_id.value(), source.value(), navigation->uri}));
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                          "android.navigation.push request=%s source=%s uri=%s accepted=%d",
                          request_id.value().wire().c_str(), source.value().wire().c_str(),
                          navigation->uri.c_str(), accepted ? 1 : 0);
#endif
      return;
    }
    if (auto* navigation = std::get_if<ja::NavigationClose>(&message)) {
      auto request_id = qc::RequestId::parse(navigation->requestId);
      auto source = qc::SurfaceId::parse(navigation->sourceSurfaceId);
      if (!request_id || !source) return;
      navigation_sources_[request_id.value().wire()] = source.value().wire();
      const auto accepted = controller_->enqueue(qs::SurfaceRequest(
          qs::NavigationCloseRequest{request_id.value(), source.value()}));
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                          "android.navigation.close request=%s source=%s accepted=%d",
                          request_id.value().wire().c_str(), source.value().wire().c_str(),
                          accepted ? 1 : 0);
#endif
      return;
    }
    if (auto* feature = std::get_if<ja::FeatureRequest>(&message)) {
      if (feature_registry_ == nullptr || runtime_abi_ == nullptr) return;
      const auto request_id = qc::RequestId::parse(feature->requestId);
      const auto surface_id = qc::SurfaceId::parse(feature->surfaceId);
      if (!request_id || !surface_id) return;

      qcf::Request request{
          request_id.value(), surface_id.value(), qcf::ModuleId::kSystemPrompt,
          qcf::Method::kShowToast, {}, std::nullopt, 0, std::nullopt, {}, {},
          std::nullopt, 0, {}, std::nullopt, std::nullopt, std::nullopt};
      request.module = feature->module == ja::FeatureModule::Prompt
                           ? qcf::ModuleId::kSystemPrompt
                           : feature->module == ja::FeatureModule::Fetch
                                 ? qcf::ModuleId::kSystemFetch
                                 : qcf::ModuleId::kSystemFile;
      switch (feature->method) {
        case ja::FeatureMethod::Alert:
          request.method = qcf::Method::kAlert;
          break;
        case ja::FeatureMethod::Confirm:
          request.method = qcf::Method::kConfirm;
          break;
        case ja::FeatureMethod::Fetch:
          request.method = qcf::Method::kFetch;
          break;
        case ja::FeatureMethod::FetchCancel:
          request.method = qcf::Method::kFetchCancel;
          break;
        case ja::FeatureMethod::FileRead:
          request.method = qcf::Method::kFileRead;
          break;
        case ja::FeatureMethod::FileWrite:
          request.method = qcf::Method::kFileWrite;
          break;
        case ja::FeatureMethod::FileExists:
          request.method = qcf::Method::kFileExists;
          break;
        case ja::FeatureMethod::FileDelete:
          request.method = qcf::Method::kFileDelete;
          break;
        case ja::FeatureMethod::OpenUrl:
        case ja::FeatureMethod::WebviewOpen:
          // B6 URL providers are not part of this Android batch yet. Route
          // these public ABI requests to an unregistered Core module so they
          // complete as typed unsupported instead of entering a file provider.
          request.module = qcf::ModuleId::kPageHost;
          request.method = qcf::Method::kShowToast;
          break;
      }
      request.text = feature->text;
      request.url = feature->url.empty() ? std::nullopt
                                         : std::optional<std::string>(feature->url);
      request.http_method = feature->httpMethod;
      request.headers.reserve(feature->headers.size());
      for (const auto& header : feature->headers)
        request.headers.push_back({header.name, header.value});
      request.body = feature->body;
      request.timeout_ms = feature->timeoutMs;
      request.response_type = feature->responseType;
      request.cancel_request_id = std::nullopt;
      if (!feature->targetRequestId.empty()) {
        if (const auto target = qc::RequestId::parse(feature->targetRequestId))
          request.cancel_request_id = target.value();
      }
      request.path = feature->path.empty() ? std::nullopt
                                           : std::optional<std::string>(feature->path);
      request.data = feature->data;

      std::optional<qcf::Result> result;
      if (request.method == qcf::Method::kFetchCancel) {
        if (!request.cancel_request_id) {
          result = qcf::Result{request.request_id, request.surface_id, qcf::Status::kFailed,
                    std::nullopt,
                    qcf::Error{"INVALID_ARGUMENT", "target request id is required", false},
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, std::nullopt};
        } else {
          result = feature_registry_->cancel(request.cancel_request_id.value(),
                                              request.surface_id);
          result->request_id = request.request_id;
        }
      } else {
        result = feature_registry_->invoke(request);
      }
      std::optional<ja::MessageRuntimeError> error;
      if (result->error) {
        error = ja::MessageRuntimeError{
            result->error->code, result->error->message, result->error->retryable,
            std::nullopt, result->request_id.wire(), std::nullopt, std::nullopt};
      }
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                          "android.feature.result request=%s surface=%s module=%d method=%d status=%s",
                          feature->requestId.c_str(), feature->surfaceId.c_str(),
                          static_cast<int>(request.module), static_cast<int>(request.method),
                          std::string(qcf::status_wire(result->status)).c_str());
#endif
      const auto posted = runtime_abi_->postCallback(ja::JsInboundMessage{
          ja::FeatureResult{feature->requestId, feature->surfaceId,
                            std::string(qcf::status_wire(result->status)),
                            result->confirmed, result->http_status,
                            result->response_body, result->response_is_json,
                            result->file_data, result->file_exists, std::move(error)}});
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                          "android.feature.callback posted=%d error=%s",
                          posted.ok ? 1 : 0,
                          posted.error ? std::string(ja::abiErrorCodeName(
                              posted.error->code)).c_str() : "");
#endif
      return;
    }
    if (auto* render = std::get_if<ja::SubmitRenderTransaction>(&message)) {
      auto surface = qc::SurfaceId::parse(render->surfaceId);
      auto transaction = qc::TransactionId::parse(render->transactionId);
      if (!surface || !transaction) return;
      std::vector<qc::runtime_tree::BindingUpdate> updates;
      std::vector<qc::runtime_tree::InstantiateBlockRequest> block_instantiates;
      std::vector<qc::BlockInstanceId> block_removes;
      std::vector<qc::runtime_tree::MoveBlockRequest> block_moves;
      std::vector<ja::HandlerBinding> js_block_handlers;
      std::vector<std::string> js_unbind_handlers;
      const auto parse_owner = [](const std::string& wire)
          -> std::optional<qc::OwnerInstanceId> {
        if (const auto component = qc::ComponentInstanceId::parse(wire)) {
          return qc::OwnerInstanceId(component.value());
        }
        if (const auto block = qc::BlockInstanceId::parse(wire)) {
          return qc::OwnerInstanceId(block.value());
        }
        return std::nullopt;
      };
      for (const auto& operation : render->operations) {
        const auto* update = std::get_if<ja::UpdateBindingOperation>(&operation);
        if (update != nullptr) {
          auto owner = qc::ComponentInstanceId::parse(update->ownerInstanceId);
          if (!owner) continue;
          updates.push_back({owner.value(), update->templateBindingId,
                             std::visit([](const auto& item)
                                        -> qc::runtime_tree::BindingValue { return item; },
                                        update->value)});
          continue;
        }
        if (const auto* instantiate =
                std::get_if<ja::InstantiateBlockOperation>(&operation)) {
        const auto block_id = qc::BlockInstanceId::parse(instantiate->blockInstanceId);
        const auto template_id = qc::TemplateBlockId::from(instantiate->templateBlockId);
        const auto parent_template_id =
            qc::TemplateNodeId::from(instantiate->parent.templateNodeId);
        const auto parent_owner = parse_owner(instantiate->parent.ownerInstanceId);
        if (!block_id || !template_id || !parent_template_id || !parent_owner) return;
        std::map<std::uint64_t, qc::runtime_tree::BindingValue> block_bindings;
        for (const auto& [id, value] : instantiate->initialBindings) {
          block_bindings.emplace(id, std::visit(
              [](const auto& item) -> qc::runtime_tree::BindingValue {
                return item;
              }, value));
        }
        std::vector<qc::runtime_tree::HandlerRegistration> block_handlers;
        for (const auto& binding : instantiate->handlers) {
          const auto owner = qc::BlockInstanceId::parse(binding.ownerInstanceId);
          const auto handler_template =
              qc::TemplateHandlerId::from(binding.templateHandlerId);
          const auto handler_id = qc::HandlerId::parse(binding.handlerId);
          if (!owner || !handler_template || !handler_id) return;
          block_handlers.push_back(
              {owner.value(), handler_template.value(), handler_id.value()});
          js_block_handlers.push_back(binding);
          block_handler_ids_[block_id.value().wire()].push_back(binding.handlerId);
        }
        qc::runtime_tree::BlockKey key = std::string("");
        if (instantiate->key.has_value()) {
          key = std::visit([](const auto& item) -> qc::runtime_tree::BlockKey {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, std::string>) {
              return item;
            } else {
              return static_cast<std::int64_t>(item);
            }
          }, *instantiate->key);
        }
        block_instantiates.push_back({
            template_id.value(), block_id.value(),
            {parent_owner.value(), parent_template_id.value()}, instantiate->index,
            std::move(key), std::move(block_bindings), std::move(block_handlers)});
        continue;
          continue;
        }
        if (const auto* remove = std::get_if<ja::RemoveBlockOperation>(&operation)) {
        const auto block_id = qc::BlockInstanceId::parse(remove->blockInstanceId);
        if (!block_id) return;
        block_removes.push_back(block_id.value());
        auto handlers = block_handler_ids_.find(block_id.value().wire());
        if (handlers != block_handler_ids_.end()) {
          js_unbind_handlers.insert(js_unbind_handlers.end(),
                                    handlers->second.begin(), handlers->second.end());
          block_handler_ids_.erase(handlers);
        }
        continue;
          continue;
        }
        if (const auto* move = std::get_if<ja::MoveBlockOperation>(&operation)) {
        const auto block_id = qc::BlockInstanceId::parse(move->blockInstanceId);
        const auto parent_template_id = qc::TemplateNodeId::from(move->parent.templateNodeId);
        const auto parent_owner = parse_owner(move->parent.ownerInstanceId);
        if (!block_id || !parent_template_id || !parent_owner) return;
        block_moves.push_back({block_id.value(),
                               {parent_owner.value(), parent_template_id.value()},
                               move->index});
        }
      }
      const auto page_template = [&]() -> std::optional<std::string> {
        std::lock_guard lock(pages_mutex_);
        const auto found = pages_.find(render->surfaceId);
        if (found == pages_.end() || !found->second) return std::nullopt;
        return (*found->second).template_id();
      }();
      if (page_template && handler_binding_sink_ && !js_block_handlers.empty()) {
        handler_binding_sink_(surface.value().wire(), *page_template,
                             std::move(js_block_handlers));
      }
      if (handler_unbind_sink_ && !js_unbind_handlers.empty()) {
        handler_unbind_sink_(surface.value().wire(), std::move(js_unbind_handlers));
      }
      std::optional<qc::RequestId> causal;
      if (render->requestId) causal = qc::RequestId::parse(*render->requestId).value();
      auto submitted = coordinator_->submit(qr::RenderTransactionIntent{
          surface.value(), transaction.value(), render->revision, causal,
          std::move(updates), std::move(block_instantiates),
          std::move(block_removes), std::move(block_moves)});
#if defined(__ANDROID__)
      __android_log_print(
          ANDROID_LOG_INFO, "QuickAppKit",
          "android.render.submit surface=%s transaction=%s revision=%llu ok=%d error=%s",
          render->surfaceId.c_str(), render->transactionId.c_str(),
          static_cast<unsigned long long>(render->revision),
          submitted.has_value() ? 1 : 0,
          submitted.has_value() ? ""
                                : std::string(qc::to_wire(submitted.error().code)).c_str());
#endif
    }
  }

  CoreMailbox& mailbox_;
  qc::event::EventRouter* event_router_{nullptr};
  qr::MountCoordinator* coordinator_{nullptr};
  qs::SurfaceController* controller_{nullptr};
  ja::RuntimeAbiService* runtime_abi_{nullptr};
  qcf::ModuleRegistry* feature_registry_{nullptr};
  std::mutex pages_mutex_;
  std::map<std::string, qp::PageIrHandle, std::less<>> pages_;
  std::map<std::string, std::string, std::less<>> navigation_sources_;
  std::map<std::string, std::vector<std::string>, std::less<>> block_handler_ids_;
  HandlerBindingSink handler_binding_sink_;
  HandlerUnbindSink handler_unbind_sink_;
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
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_ERROR, "QuickAppKit",
                            "android.runtime.exception error=%s", error.what());
#endif
        if (gateway) gateway->notifyFailed("RUNTIME_FAILED", error.what());
        running.store(false);
      } catch (...) {
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_ERROR, "QuickAppKit",
                            "android.runtime.exception error=unknown");
#endif
        if (gateway) gateway->notifyFailed("RUNTIME_FAILED", "unknown runtime error");
        running.store(false);
      }
    });
  }

  void run(std::string path) {
    androidStage("run.begin");
    factory = std::make_unique<qc::AppRuntimeFactory>();
    identity = std::move(factory->create()).value();
    androidStage("factory.created");
    auto bytes = readFile(path);
    androidStage("rpk.read");
    auto source = std::make_shared<MemorySource>(std::move(bytes));
    qp::RuntimeComposition composition{
        "quickapp-kit-runtime-v1", "quickapp-kit-js-engine-v1",
        {"View", "Text", "Button", "Image", "Input", "Switch", "Slider", "Picker",
         "List", "Scroll", "Video", "Tabs"},
        {"system.prompt", "system.router", "system.shortcut", "system.fetch",
         "system.device", "system.file"}};
    loader = std::move(qp::PackageLoader::create(
                         source, identity->request_ids(), std::move(composition)))
                 .value();
    androidStage("rpk.loader.created");
    if (!loader->open([this](auto result) {
          if (result) package = std::move(result).value();
          else startup_error = result.error().message;
        }) || !package) {
      throw std::runtime_error(startup_error.empty() ? "RPK open failed" : startup_error);
    }
    androidStage("rpk.verified");

    auto provider = std::make_unique<qj::QuickJsEngineProvider>();
    Clock clock;
    TraceSink trace_sink;
    auto registration = qj::TraceSinkRegistration::admit(
        trace_sink, {.nonblocking = true, .noReentry = true});
    if (!registration.ok()) throw std::runtime_error("TraceSink registration failed");
    qj::JsEngineConfig engine_config;
    engine_config.expectedEngine = provider->describe();
    engine_config.limits.maxPendingTasks = 64;
    engine = std::make_unique<qj::JsEngineService>(
        identity->id().wire(), std::move(provider), engine_config, clock,
        std::move(registration).value(),
        qj::ObservationConfig{false, "run:android-a1", "android-monotonic", 0});
    std::promise<qj::ServiceResult> started_result;
    auto started_future = started_result.get_future();
    if (!engine->start([&](qj::ServiceResult result) {
          started_result.set_value(std::move(result));
        }) || !started_future.get().ok()) {
      throw std::runtime_error("QuickJS start failed");
    }
    androidStage("js.started");

    auto* surface_sink = gateway.get();
    (void)surface_sink;
    counters = std::make_unique<qc::RuntimeCounters>();
    mount_results = std::make_unique<MountResults>();
    auto measure = std::make_unique<platform::MeasurePort>();
    auto mount = std::make_unique<platform::MountPort>(*gateway);
    auto render_results = std::make_unique<RenderResults>();
    render_results_raw = render_results.get();
    auto initial_results = std::make_unique<ControllerInitialResults>(&controller);
    core_ingress = std::make_unique<JsCoreIngress>(mailbox);
    event_router = std::make_unique<qc::event::EventRouter>(*core_ingress);
    core_ingress->bindEventRouter(*event_router);
    core_ingress->setViewport(viewport_width, viewport_height);
    auto coordinator_result = qr::MountCoordinator::create(
        {&identity->request_ids(), counters.get(), std::move(measure),
         std::move(mount), std::move(initial_results), nullptr, nullptr,
         event_router.get(), std::move(render_results)});
    if (!coordinator_result) throw std::runtime_error("MountCoordinator create failed");
    coordinator = std::move(coordinator_result).value();
    androidStage("coordinator.created");
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
    AppState app_state;
    auto controller_result = qs::SurfaceController::create(
        {&app_state, &identity->request_ids(), std::move(pages),
         std::move(platform_port), std::move(page_lifecycle),
         std::move(initial_pipeline), std::move(operations),
         std::make_unique<ControllerStatus>(),
         std::make_unique<ControllerLifecycleResults>(), counters.get()});
    if (!controller_result) throw std::runtime_error("SurfaceController create failed");
    controller = std::move(controller_result).value();
    androidStage("surface_controller.created");

    feature_registry = std::make_unique<qcf::ModuleRegistry>();
    feature_provider = std::make_unique<platform::AndroidFeatureProvider>();
    if (!feature_registry->register_provider(qcf::ModuleId::kSystemPrompt,
                                             *feature_provider) ||
        !feature_registry->register_provider(qcf::ModuleId::kSystemFetch,
                                             *feature_provider) ||
        !feature_registry->register_provider(qcf::ModuleId::kSystemFile,
                                             *feature_provider)) {
      throw std::runtime_error("Android feature provider registration failed");
    }
    androidStage("feature.registry.created");

    setupJs();
    androidStage("js.setup");
    core_ingress->bindHandlerSinks(
        [this](std::string surface_id, std::string template_id,
               std::vector<ja::HandlerBinding> bindings) {
          if (!engine) return;
          const auto task = engine->post(
              [this, surface_id = std::move(surface_id),
               template_id = std::move(template_id),
               bindings = std::move(bindings)](qj::JsEnginePort&,
                                               const qj::JsContextRef&) mutable {
                if (!modules || !handler_registry || !vm) return;
                const auto definition = modules->pageDefinitionForSurfaceOnExecutor(
                    surface_id, template_id);
                if (!definition) return;
                for (const auto& binding : bindings) {
                  const auto method = modules->handlerMethodNameOnExecutor(
                      *definition, binding.templateHandlerId);
                  auto page_vm = vm->pageVmOnExecutor(surface_id);
                  const auto bound = method
                                         && page_vm.ok()
                                         ? handler_registry->bind(
                                               surface_id, binding.handlerId,
                                               *method,
                                               std::move(page_vm).value())
                                         : false;
#if defined(__ANDROID__)
                  __android_log_print(
                      ANDROID_LOG_INFO, "QuickAppKit",
                      "android.event.block_handler_bind surface=%s handler=%s method=%s bound=%d",
                      surface_id.c_str(), binding.handlerId.c_str(),
                      method ? method->c_str() : "", bound ? 1 : 0);
#endif
                }
              });
          if (task.status != qj::PostStatus::Accepted) {
#if defined(__ANDROID__)
            __android_log_print(ANDROID_LOG_ERROR, "QuickAppKit",
                                "android.event.block_handler_queue_rejected surface=%s",
                                surface_id.c_str());
#endif
          }
        },
        [this](std::string surface_id, std::vector<std::string> handlers) {
          if (!engine) return;
          const auto task = engine->post(
              [this, surface_id = std::move(surface_id),
               handlers = std::move(handlers)](qj::JsEnginePort&, const qj::JsContextRef&) {
                if (!handler_registry) return;
                for (const auto& handler : handlers)
                  handler_registry->unbind(surface_id, handler);
              });
          (void)task;
        });
    core_ingress->bind(*coordinator, *controller, *runtime_abi,
                       *feature_registry);
    if (!postRoot()) throw std::runtime_error("Android root request rejected");
    androidStage("root.enqueued");
    running.store(true);
    bool first_loop = true;
    while (!stopping.load()) {
      if (first_loop) androidStage("core.loop.before");
      mailbox.drain(128);
      if (first_loop) androidStage("core.loop.mailbox.drained");
      if (controller) {
        auto drained = controller->drain();
        if (!drained) {
#if defined(__ANDROID__)
          __android_log_print(ANDROID_LOG_ERROR, "QuickAppKit",
                              "android.core.controller.failed error=%s",
                              std::string(drained.error().message).c_str());
#endif
          throw std::runtime_error(std::string(drained.error().message));
        }
        if (drained.value() != 0) {
          androidStage("core.loop.controller.work");
#if defined(__ANDROID__)
          __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                              "android.core.controller.drained count=%zu",
                              drained.value());
#endif
        }
      }
      if (first_loop) {
        androidStage("core.loop.controller.drained");
        first_loop = false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (mailbox.depth() == 0 && controller && !controller->snapshot().accepting &&
          stopping.load()) break;
    }
    cleanup();
  }

  bool postRoot() {
    return static_cast<bool>(controller->enqueue(qs::SurfaceRequest(
        qs::RootSurfaceRequest{parseRequest("req:android-root"),
                               package->entry_route()})));
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
      if (!runtime_abi->startOnExecutor(js, context, ja::kRuntimeAbiIdentity).ok())
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
        if (handler_registry) {
          const auto dispatched = handler_registry->dispatchOnExecutor(event);
#if defined(__ANDROID__)
          __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                              "android.event.handler_execute surface=%s handler=%s dispatched=%d",
                              event.surfaceId.c_str(), event.handlerId.c_str(),
                              dispatched ? 1 : 0);
#endif
        }
      };
      slots.renderTransactionResult = [](const ja::RenderTransactionResult&) {};
      if (!runtime_abi->registerConsumersOnExecutor(std::move(slots)).ok() ||
          !vm->startOnExecutor(js, context))
        throw std::runtime_error("JS consumer registration failed");

      std::vector<std::string> module_ids;
      std::set<std::string, std::less<>> visited;
      std::set<std::string, std::less<>> visiting;
      std::function<void(const std::string&)> visit = [&](const std::string& module_id) {
        if (visited.contains(module_id)) return;
        if (!visiting.insert(module_id).second)
          throw std::runtime_error("RPK module dependency cycle");
        const auto found = package->modules().find(module_id);
        if (found == package->modules().end())
          throw std::runtime_error("RPK module dependency missing");
        for (const auto& dependency : found->second.dependencies) visit(dependency);
        visiting.erase(module_id);
        visited.insert(module_id);
        module_ids.push_back(module_id);
      };
      visit("@quickapp-kit/app");
      for (const auto& [module_id, descriptor] : package->modules()) {
        if (descriptor.kind == qp::ModuleKind::kShared) visit(module_id);
      }
      auto load_next = std::make_shared<std::function<void(std::size_t)>>();
      *load_next = [this, module_ids = std::move(module_ids), load_next](
                       std::size_t index) mutable {
        if (index >= module_ids.size()) {
          vm->onAppContext({package->package_id(), "1.0.0", "1", 1,
                            {"system.router", "system.prompt", "system.device",
                             "system.fetch", "system.file"}});
          vm->onVmInitialization({
              "req:" + std::to_string(vm_request_sequence.fetch_add(
                  1, std::memory_order_relaxed)),
              "app", std::nullopt});
          js_setup_finished.store(true, std::memory_order_release);
          androidStage("js.setup.finished");
          return;
        }
        const auto module_id = module_ids[index];
        const auto found = package->modules().find(module_id);
        if (found == package->modules().end()) {
          js_setup_failed.store(true, std::memory_order_release);
          return;
        }
        const auto module_kind = found->second.kind == qp::ModuleKind::kShared
                                     ? std::string("shared")
                                     : std::string("app");
        const auto expected_bootstrap = module_kind == "app"
            ? std::optional<ja::BootstrapExpectation>{
                  ja::BootstrapExpectation{"app", module_id, std::nullopt}}
            : std::nullopt;
        const auto accepted = loader->load_module(
            {module_id, std::nullopt},
            [this, module_id, module_kind, expected_bootstrap, load_next,
             index](auto result) mutable {
              if (!result) {
                js_setup_failed.store(true, std::memory_order_release);
                return;
              }
              auto module = std::move(result).value();
              ja::LoadVerifiedModule message;
              message.requestId = "req:j-" +
                                  std::to_string(js_module_sequence.fetch_add(
                                      1, std::memory_order_relaxed));
              message.packageId = module.package_id();
              message.moduleKind = module_kind;
              message.moduleId = module.module_id();
              message.cacheScope = "appRuntime";
              message.dependencies = module.dependencies();
              message.bundle = {module.descriptor().path, module.descriptor().byte_length,
                                module.descriptor().sha256,
                                std::make_shared<const std::vector<std::uint8_t>>(
                                    *module.bytes())};
              message.expectedBootstrap = expected_bootstrap;
              modules->onLoadVerifiedModule(message);
              (*load_next)(index + 1);
            });
        if (!accepted) js_setup_failed.store(true, std::memory_order_release);
      };
      (*load_next)(0);
    });
    if (setup.status != qj::PostStatus::Accepted) throw std::runtime_error("JS setup enqueue failed");
    waitForJsSetup();
  }

  void waitForJsSetup() {
    for (std::size_t i = 0; i < 5000; ++i) {
      if (js_setup_failed.load(std::memory_order_acquire))
        throw std::runtime_error("JS module setup failed");
      if (js_setup_finished.load(std::memory_order_acquire)) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("JS setup timeout");
  }

  qc::EnqueueResult postPageCommand(qs::PageCommand&& command) {
    androidStage("page.command.received");
    if (!engine || !modules || !vm || !runtime_abi) return qc::EnqueueResult::failure(
        qc::RuntimeError::simple(qc::RuntimeErrorCode::kPlatformRejected, "JS page service unavailable"));
    auto task = engine->post([this, command = std::move(command)](
                                 qj::JsEnginePort&, const qj::JsContextRef&) mutable {
      androidStage("page.command.js");
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
        if (!runtime_abi->openSurfaceOnExecutor(start->surface_id.wire()).ok() ||
            !modules->openSurfaceOnExecutor(start->surface_id.wire())) {
          complete(command, false);
          return;
        }
        const auto& module = start->page.module;
        modules->onLoadVerifiedModule(ja::LoadVerifiedModule{
            "req:j-" + std::to_string(js_module_sequence.fetch_add(
                1, std::memory_order_relaxed)), module.package_id(), "page", module.module_id(),
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
        complete(command, true);
        androidStage("page.start.completed");
      } else if (auto* hook = std::get_if<qs::PageHookCommand>(&command)) {
        if (hook->hook == qs::PageHook::kOnDestroy) {
          if (handler_registry) handler_registry->closeSurface(hook->surface_id.wire());
          event_router->closeSurface(hook->surface_id);
          if (feature_registry) feature_registry->teardown(hook->surface_id);
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
    androidStage("initial.command.received");
    if (!coordinator || !engine) return qc::EnqueueResult::failure(platform::platformError("Android initial services unavailable"));
    const auto surface = command.surface_id;
    const auto page_ir = command.page_ir;
    auto posted = coordinator->post(std::move(command));
    androidStage("initial.command.coordinator");
    if (!posted) return posted;
    auto task = engine->post([this, surface, page_ir](qj::JsEnginePort&, const qj::JsContextRef&) {
      androidStage("initial.command.js");
      vm->onVmInitialization({
          "req:" + std::to_string(vm_request_sequence.fetch_add(
              1, std::memory_order_relaxed)),
          "page", surface.wire()});
      const auto page_vm = vm->pageVmOnExecutor(surface.wire());
      androidStage(page_vm.ok() ? "page.vm.ready" : "page.vm.failed");
      if (!modules || !handler_registry) return;
      const auto definition = modules->pageDefinitionForSurfaceOnExecutor(
          surface.wire(), page_ir->template_id());
      if (!definition) return;
      const auto handlers = modules->handlerBindingsOnExecutor(
          *definition, "cmp:" + surface.wire());
      if (!handlers.ok()) return;
      for (const auto& binding : handlers.value()) {
        const auto method = modules->handlerMethodNameOnExecutor(
            *definition, binding.templateHandlerId);
        auto vm_value = vm->pageVmOnExecutor(surface.wire());
        const auto bound = method && vm_value.ok()
                               ? handler_registry->bind(
                                     surface.wire(), binding.handlerId, *method,
                                     std::move(vm_value).value())
                               : false;
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                            "android.event.handler_bind surface=%s handler=%s method=%s bound=%d method_ok=%d vm_ok=%d",
                            surface.wire().c_str(), binding.handlerId.c_str(),
                            method ? method->c_str() : "", bound ? 1 : 0,
                            method ? 1 : 0, vm_value.ok() ? 1 : 0);
#endif
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
    androidStage(completed ? "surface.operation.completed"
                           : "surface.operation.failed");
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                        "android.surface.operation kind=%d request=%s target=%s error=%s",
                        static_cast<int>(kind), request.wire().c_str(),
                        target ? target->wire().c_str() : "",
                        error ? std::string(error->message).c_str() : "");
#endif
    if ((kind != qs::SurfaceOperationKind::kPush &&
         kind != qs::SurfaceOperationKind::kClose) ||
        !runtime_abi) return;
    auto source = core_ingress->takeNavigationSource(request.wire());
    if (!source) return;
    std::optional<ja::MessageRuntimeError> mapped;
    if (error) mapped = ja::MessageRuntimeError{
        std::string(qc::to_wire(error->code)), std::string(error->message), error->retryable,
        std::nullopt, request.wire(), std::nullopt, std::nullopt};
    if (kind == qs::SurfaceOperationKind::kPush) {
      static_cast<void>(runtime_abi->postCallback(ja::JsInboundMessage{
          ja::NavigationPushResult{request.wire(), *source,
                                    completed ? "presented" : "failed",
                                    target ? std::optional<std::string>(target->wire())
                                           : std::nullopt,
                                    std::move(mapped)}}));
    } else {
      std::optional<std::string> revealed = target
                                                ? std::optional<std::string>(target->wire())
                                                : std::nullopt;
      const auto reveal = navigation_reveals.find(*source);
      if (reveal != navigation_reveals.end()) {
        revealed = reveal->second;
        navigation_reveals.erase(reveal);
      }
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                          "android.navigation.close.result request=%s source=%s revealed=%s completed=%d",
                          request.wire().c_str(), source->c_str(),
                          revealed ? revealed->c_str() : "", completed ? 1 : 0);
#endif
      static_cast<void>(runtime_abi->postCallback(ja::JsInboundMessage{
          ja::NavigationCloseResult{request.wire(), *source,
                                    completed ? "closed" : "failed",
                                    std::move(revealed),
                                    std::move(mapped)}}));
    }
  }

  void acceptSurfaceResult(std::string request_id, int kind,
                           std::string target, std::optional<std::string> source,
                           std::optional<std::string> reveal, int visibility,
                           bool completed, std::optional<std::string> code,
                           std::optional<std::string> message) noexcept {
    androidStage("surface.result.received");
    mailbox.post([this, request_id = std::move(request_id), kind,
                  target = std::move(target), source = std::move(source),
                  reveal = std::move(reveal), visibility, completed,
                  code = std::move(code), message = std::move(message)]() mutable {
      androidStage("surface.result.core");
      auto request = qc::RequestId::parse(request_id);
      auto target_id = qc::SurfaceId::parse(target);
      if (!request || !target_id || !controller) return;
      std::optional<qc::RuntimeError> error;
      if (!completed) error = platform::platformError(message.value_or("Android Surface operation failed"));
      if (kind == 0) static_cast<void>(controller->enqueue(qs::SurfaceCommandResult{
          request.value(), qs::SurfaceCommandKind::kCreate, target_id.value(),
          std::nullopt, std::nullopt, std::nullopt, completed, std::move(error)}));
      else if (kind == 1) {
        std::optional<qc::SurfaceId> source_id;
        if (source) {
          auto parsed = qc::SurfaceId::parse(*source);
          if (parsed) source_id = parsed.value();
        }
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
        std::optional<qc::SurfaceId> source_id;
        std::optional<qc::SurfaceId> reveal_id;
        if (source) {
          auto parsed = qc::SurfaceId::parse(*source);
          if (parsed) source_id = parsed.value();
        }
        if (reveal) {
          auto parsed = qc::SurfaceId::parse(*reveal);
          if (parsed) reveal_id = parsed.value();
        }
        if (source && reveal_id) navigation_reveals[*source] = reveal_id->wire();
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
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                        "android.mount.result.received surface=%s revision=%llu attempt=%s source=%s mounted=%d",
                        surface_id.c_str(), static_cast<unsigned long long>(revision),
                        attempt.c_str(), source.c_str(), mounted ? 1 : 0);
#endif
    mailbox.post([this, surface_id = std::move(surface_id), revision,
                  attempt = std::move(attempt), source = std::move(source), mounted,
                  code = std::move(code), message = std::move(message)]() mutable {
      auto surface = qc::SurfaceId::parse(surface_id);
      auto mount_attempt = qc::MountAttemptId::parse(attempt);
      if (!surface || !mount_attempt || !coordinator) {
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_ERROR, "QuickAppKit",
                            "android.mount.result.rejected_parse surface=%s attempt=%s source=%s",
                            surface_id.c_str(), attempt.c_str(), source.c_str());
#endif
        return;
      }
      std::optional<qr::RenderSourceId> source_id;
      if (source.starts_with("txn:")) {
        auto parsed = qc::TransactionId::parse(source);
        if (!parsed) {
#if defined(__ANDROID__)
          __android_log_print(ANDROID_LOG_ERROR, "QuickAppKit",
                              "android.mount.result.rejected_transaction source=%s",
                              source.c_str());
#endif
          return;
        }
        source_id.emplace(parsed.value());
      } else {
        auto parsed = qc::RequestId::parse(source);
        if (!parsed) {
#if defined(__ANDROID__)
          __android_log_print(ANDROID_LOG_ERROR, "QuickAppKit",
                              "android.mount.result.rejected_request source=%s",
                              source.c_str());
#endif
          return;
        }
        source_id.emplace(parsed.value());
      }
      const auto accepted = coordinator->accept(qr::MountTransactionResult{
          surface.value(), revision, mount_attempt.value(), *source_id, mounted,
          mounted ? std::nullopt
                  : std::optional<qc::RuntimeError>(platform::platformError(
                        message.value_or("Android Mount failed")))});
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                          "android.mount.result.accepted=%d", accepted ? 1 : 0);
#endif
    });
  }

  void dispatchClick(std::string surface_id, std::string node_id,
                     std::uint64_t timestamp_ns) noexcept {
    dispatchEvent(std::move(surface_id), std::move(node_id), "click",
                  std::nullopt, "value", std::nullopt, false, false, false, false,
                  0, 0, 0, false, timestamp_ns);
  }

  void dispatchEvent(std::string surface_id, std::string node_id,
                     std::string event_type, std::optional<std::string> value,
                     std::string number_name, std::optional<double> number,
                     bool checked, bool has_checked,
                     bool from_user, bool has_from_user,
                     double scroll_offset, double content_size,
                     double viewport_size, bool has_scroll_metrics,
                     std::uint64_t timestamp_ns) noexcept {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                        "android.event.%s.received surface=%s node=%s",
                        event_type.c_str(), surface_id.c_str(), node_id.c_str());
#endif
    mailbox.post([this, surface_id = std::move(surface_id), node_id = std::move(node_id),
                  event_type = std::move(event_type), value = std::move(value),
                  number_name = std::move(number_name), number = std::move(number),
                  checked, has_checked, from_user, has_from_user,
                  scroll_offset, content_size, viewport_size, has_scroll_metrics,
                  timestamp_ns]() mutable {
      auto surface = qc::SurfaceId::parse(surface_id);
      auto node = qc::NodeId::parse(node_id);
      if (!surface || !node || !event_router) return;
      auto request = qc::RequestId::parse(
          "req:p-" + std::to_string(platform_event_sequence.fetch_add(
              1, std::memory_order_relaxed)));
      if (!request) return;
      qp::EventType type = qp::EventType::kClick;
      if (event_type == "input") type = qp::EventType::kInput;
      else if (event_type == "change") type = qp::EventType::kChange;
      else if (event_type == "focus") type = qp::EventType::kFocus;
      else if (event_type == "scroll") type = qp::EventType::kScroll;
      else if (event_type == "scrollend") type = qp::EventType::kScrollEnd;
      else if (event_type == "scrolltop") type = qp::EventType::kScrollTop;
      else if (event_type == "scrollbottom") type = qp::EventType::kScrollBottom;
      else if (event_type == "prepared") type = qp::EventType::kPrepared;
      else if (event_type == "start") type = qp::EventType::kStart;
      else if (event_type == "pause") type = qp::EventType::kPause;
      else if (event_type == "finish") type = qp::EventType::kFinish;
      else if (event_type == "error") type = qp::EventType::kError;
      else if (event_type == "timeupdate") type = qp::EventType::kTimeUpdate;
      else if (event_type != "click") return;
      qc::RuntimeValue::Object payload;
      if (value) {
        auto encoded = qc::RuntimeValue::utf8_string(*value);
        if (!encoded) return;
        payload.emplace("value", std::move(encoded).value());
      }
      if (number) {
        auto encoded = qc::RuntimeValue::finite_number(*number);
        if (!encoded) return;
        payload.emplace(number_name.empty() ? "value" : number_name,
                        std::move(encoded).value());
      }
      if (has_checked) payload.emplace("checked", qc::RuntimeValue::boolean(checked));
      if (has_from_user) {
        payload.emplace("isFromUser", qc::RuntimeValue::boolean(from_user));
      }
      if (has_scroll_metrics) {
        auto offset = qc::RuntimeValue::finite_number(scroll_offset);
        auto content = qc::RuntimeValue::finite_number(content_size);
        auto viewport = qc::RuntimeValue::finite_number(viewport_size);
        if (!offset || !content || !viewport) return;
        payload.emplace("scrollOffset", std::move(offset).value());
        payload.emplace("contentSize", std::move(content).value());
        payload.emplace("viewportSize", std::move(viewport).value());
      }
      const auto dispatched = event_router->dispatch(qc::event::PlatformInputMessage{
          request.value(), surface.value(), node.value(), type, timestamp_ns,
          std::move(payload)});
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "QuickAppKit",
                          "android.event.%s.dispatched=%d", event_type.c_str(),
                          dispatched ? 1 : 0);
#endif
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
      const auto teardown_services = [this] {
        if (handler_registry) handler_registry->stopOnExecutor();
        if (vm) vm->stopOnExecutor();
        if (transaction_builder) transaction_builder->stopOnExecutor();
        if (binding_stage) binding_stage->stopOnExecutor();
        if (page_controls) page_controls->stopOnExecutor();
        if (runtime_abi) runtime_abi->stopOnExecutor();
        if (modules) modules->stopOnExecutor();
        if (facades) facades->stopOnExecutor();
      };
      if (!engine->stop(teardown_services, [&] { stopped_result.set_value(); })) {
        teardown_services();
        stopped_result.set_value();
      }
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
    if (feature_registry) feature_registry->close();
    feature_registry.reset();
    feature_provider.reset();
    loader.reset();
    if (factory) {
      factory->stop();
      if (identity) identity->reset();
      static_cast<void>(factory->teardown());
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
  std::atomic<std::uint64_t> js_module_sequence{1};
  std::atomic<std::uint64_t> vm_request_sequence{1};
  std::atomic<std::uint64_t> platform_event_sequence{1};
  std::map<std::string, std::string, std::less<>> navigation_reveals;
  std::atomic<bool> js_setup_finished{false};
  std::atomic<bool> js_setup_failed{false};
  std::unique_ptr<qc::RuntimeCounters> counters;
  std::unique_ptr<MountResults> mount_results;
  RenderResults* render_results_raw{nullptr};
  std::unique_ptr<qr::MountCoordinator> coordinator;
  std::unique_ptr<qc::event::EventRouter> event_router;
  std::unique_ptr<JsCoreIngress> core_ingress;
  std::unique_ptr<qj::framework::StaticFacadeCatalog> facades;
  std::unique_ptr<qcf::ModuleRegistry> feature_registry;
  std::unique_ptr<platform::AndroidFeatureProvider> feature_provider;
  std::unique_ptr<ModuleCompletion> module_completion;
  qj::module::ModuleLoader* modules{nullptr};
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

void RuntimeSpine::dispatchEvent(
    std::string surface_id, std::string node_id, std::string event_type,
    std::optional<std::string> value, std::string number_name,
    std::optional<double> number,
    bool checked, bool has_checked, bool from_user, bool has_from_user,
    double scroll_offset, double content_size, double viewport_size,
    bool has_scroll_metrics,
    std::uint64_t timestamp_ns) noexcept {
  if (impl_) impl_->dispatchEvent(std::move(surface_id), std::move(node_id),
                                  std::move(event_type), std::move(value),
                                  std::move(number_name), std::move(number),
                                  checked, has_checked,
                                  from_user, has_from_user,
                                  scroll_offset, content_size, viewport_size,
                                  has_scroll_metrics, timestamp_ns);
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
