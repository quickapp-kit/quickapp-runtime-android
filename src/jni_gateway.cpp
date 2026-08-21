#include <jni.h>

#include <android/log.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "quickapp/android/runtime_spine.h"

namespace quickapp::android {
namespace {

constexpr char kLogTag[] = "QuickAppKit";

std::string string(JNIEnv* env, jstring value) {
  if (value == nullptr) return {};
  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (chars == nullptr) return {};
  std::string result(chars);
  env->ReleaseStringUTFChars(value, chars);
  return result;
}

std::optional<std::string> optionalString(JNIEnv* env, jstring value) {
  if (value == nullptr) return std::nullopt;
  return string(env, value);
}

jstring javaString(JNIEnv* env, std::string_view value) {
  return env->NewStringUTF(std::string(value).c_str());
}

class EnvScope final {
 public:
  explicit EnvScope(JavaVM* vm) : vm_(vm) {
    if (vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) != JNI_OK) {
      if (vm_->AttachCurrentThread(&env_, nullptr) == JNI_OK) attached_ = true;
    }
  }
  ~EnvScope() {
    if (attached_) vm_->DetachCurrentThread();
  }
  JNIEnv* get() const noexcept { return env_; }

 private:
  JavaVM* vm_{nullptr};
  JNIEnv* env_{nullptr};
  bool attached_{false};
};

class JniGateway final : public platform::Gateway {
 public:
  static std::shared_ptr<JniGateway> create(JNIEnv* env, jobject bridge) noexcept {
    try {
      JavaVM* vm = nullptr;
      if (env->GetJavaVM(&vm) != JNI_OK) return nullptr;
      auto value = std::shared_ptr<JniGateway>(new JniGateway(vm));
      value->bridge_ = env->NewGlobalRef(bridge);
      jclass bridge_class = env->GetObjectClass(bridge);
      value->bridge_class_ = static_cast<jclass>(env->NewGlobalRef(bridge_class));
      env->DeleteLocalRef(bridge_class);
      jclass operation_class = env->FindClass(
          "dev/quickapp/kit/android/MountOperation");
      jclass transaction_class = env->FindClass(
          "dev/quickapp/kit/android/MountTransaction");
      if (!operation_class || !transaction_class || !value->bridge_ ||
          !value->bridge_class_) return nullptr;
      value->operation_class_ =
          static_cast<jclass>(env->NewGlobalRef(operation_class));
      value->transaction_class_ =
          static_cast<jclass>(env->NewGlobalRef(transaction_class));
      env->DeleteLocalRef(operation_class);
      env->DeleteLocalRef(transaction_class);

      value->post_create_ = env->GetMethodID(
          value->bridge_class_, "postCreateSurface",
          "(Ljava/lang/String;Ljava/lang/String;)V");
      value->post_present_ = env->GetMethodID(
          value->bridge_class_, "postPresentSurface",
          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V");
      value->post_visibility_ = env->GetMethodID(
          value->bridge_class_, "postSetSurfaceVisibility",
          "(Ljava/lang/String;Ljava/lang/String;Z)V");
      value->post_close_ = env->GetMethodID(
          value->bridge_class_, "postCloseSurface",
          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
      value->post_destroy_ = env->GetMethodID(
          value->bridge_class_, "postDestroySurface",
          "(Ljava/lang/String;Ljava/lang/String;)V");
      value->post_mount_ = env->GetMethodID(
          value->bridge_class_, "postMountTransaction",
          "(Ldev/quickapp/kit/android/MountTransaction;)V");
      value->started_ = env->GetMethodID(
          value->bridge_class_, "onRuntimeStarted", "(Ljava/lang/String;)V");
      value->failed_ = env->GetMethodID(
          value->bridge_class_, "onRuntimeFailed",
          "(Ljava/lang/String;Ljava/lang/String;)V");
      value->stopped_ = env->GetMethodID(
          value->bridge_class_, "onRuntimeStopped", "(IIIIII)V");
      value->operation_constructor_ = env->GetMethodID(
          value->operation_class_, "<init>",
          "(ILjava/lang/String;Ljava/lang/String;ILjava/lang/String;IZDLjava/lang/String;FFFFI)V");
      value->transaction_constructor_ = env->GetMethodID(
          value->transaction_class_, "<init>",
          "(Ljava/lang/String;JLjava/lang/String;Ljava/lang/String;Z[Ldev/quickapp/kit/android/MountOperation;)V");
      if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
      }
      return value;
    } catch (...) {
      return nullptr;
    }
  }

  ~JniGateway() override { close(); }

  bool postCreateSurface(
      const core::surface::SurfaceCreateHostCommand& command) noexcept override {
    return call(post_create_, command.request_id.wire(), command.surface_id.wire());
  }

  bool postPresentSurface(
      const core::surface::SurfacePresentCommand& command) noexcept override {
    const bool push = command.mode == core::surface::SurfacePresentMode::kPush;
    return callPresent(command.request_id.wire(), command.target.wire(),
                       command.source ? command.source->wire() : std::string{}, push);
  }

  bool postVisibility(
      const core::surface::SurfaceVisibilityCommand& command) noexcept override {
    return callVisibility(
        command.request_id.wire(), command.surface_id.wire(),
        command.visibility == core::lifecycle::SurfaceVisibility::kVisible);
  }

  bool postCloseSurface(
      const core::surface::SurfaceCloseCommand& command) noexcept override {
    return call(post_close_, command.request_id.wire(), command.source.wire(),
                command.reveal.wire());
  }

  bool postDestroySurface(
      const core::surface::SurfaceDestroyCommand& command) noexcept override {
    return call(post_destroy_, command.request_id.wire(), command.surface_id.wire());
  }

  bool postMount(const core::render::MountTransaction& transaction) noexcept override {
    if (!open_.load(std::memory_order_acquire)) return false;
    EnvScope scope(vm_);
    JNIEnv* env = scope.get();
    if (!env) return false;
    pending_.fetch_add(1, std::memory_order_relaxed);
    bool ok = false;
    jobjectArray operations = env->NewObjectArray(
        static_cast<jsize>(transaction.operations.size()), operation_class_, nullptr);
    if (operations != nullptr) {
      ok = true;
      for (std::size_t index = 0; index < transaction.operations.size(); ++index) {
        jobject operation = makeOperation(env, transaction.operations[index]);
        if (operation == nullptr) {
          ok = false;
          break;
        }
        env->SetObjectArrayElement(operations, static_cast<jsize>(index), operation);
        env->DeleteLocalRef(operation);
      }
    }
    if (ok) {
      jstring surface = javaString(env, transaction.surface_id.wire());
      jstring attempt = javaString(env, transaction.mount_attempt_id.wire());
      jstring source = javaString(env, core::render::render_source_wire(transaction.source_id));
      jobject value = env->NewObject(
          transaction_class_, transaction_constructor_, surface,
          static_cast<jlong>(transaction.revision), attempt, source,
          static_cast<jboolean>(transaction.mode == core::render::MountMode::kFull),
          operations);
      if (value != nullptr) {
        env->CallVoidMethod(bridge_, post_mount_, value);
        env->DeleteLocalRef(value);
      } else {
        ok = false;
      }
      env->DeleteLocalRef(surface);
      env->DeleteLocalRef(attempt);
      env->DeleteLocalRef(source);
    }
    if (operations) env->DeleteLocalRef(operations);
    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
      ok = false;
    }
    pending_.fetch_sub(1, std::memory_order_relaxed);
    return ok;
  }

  void notifyStarted(std::string_view surface_id) noexcept override {
    callOne(started_, surface_id);
  }

  void notifyFailed(std::string_view code, std::string_view message) noexcept override {
    call(failed_, code, message);
  }

  void notifyStopped(std::size_t surfaces, std::size_t nodes,
                     std::size_t handlers, std::size_t pending_callbacks,
                     std::size_t js_resources,
                     std::size_t core_queue_depth) noexcept override {
    if (!open_.load(std::memory_order_acquire)) return;
    EnvScope scope(vm_);
    JNIEnv* env = scope.get();
    if (!env) return;
    env->CallVoidMethod(bridge_, stopped_, static_cast<jint>(surfaces),
                        static_cast<jint>(nodes), static_cast<jint>(handlers),
                        static_cast<jint>(pending_callbacks),
                        static_cast<jint>(js_resources),
                        static_cast<jint>(core_queue_depth));
    clearException(env);
  }

  std::size_t pendingCallbacks() const noexcept override {
    return pending_.load(std::memory_order_relaxed);
  }

  void close() noexcept override {
    if (!open_.exchange(false)) return;
    EnvScope scope(vm_);
    JNIEnv* env = scope.get();
    if (!env) return;
    if (bridge_) env->DeleteGlobalRef(bridge_);
    if (bridge_class_) env->DeleteGlobalRef(bridge_class_);
    if (operation_class_) env->DeleteGlobalRef(operation_class_);
    if (transaction_class_) env->DeleteGlobalRef(transaction_class_);
    bridge_ = nullptr;
    bridge_class_ = nullptr;
    operation_class_ = nullptr;
    transaction_class_ = nullptr;
  }

 private:
  explicit JniGateway(JavaVM* vm) : vm_(vm) {}

  static void clearException(JNIEnv* env) noexcept {
    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
    }
  }

  bool callOne(jmethodID method, std::string_view first) noexcept {
    if (!open_.load(std::memory_order_acquire)) return false;
    EnvScope scope(vm_);
    JNIEnv* env = scope.get();
    if (!env) return false;
    jstring a = javaString(env, first);
    env->CallVoidMethod(bridge_, method, a);
    env->DeleteLocalRef(a);
    const bool ok = !env->ExceptionCheck();
    clearException(env);
    return ok;
  }

  bool call(jmethodID method, std::string_view first,
            std::string_view second) noexcept {
    if (!open_.load(std::memory_order_acquire)) return false;
    EnvScope scope(vm_);
    JNIEnv* env = scope.get();
    if (!env) return false;
    jstring a = javaString(env, first);
    jstring b = javaString(env, second);
    env->CallVoidMethod(bridge_, method, a, b);
    env->DeleteLocalRef(a);
    env->DeleteLocalRef(b);
    const bool ok = !env->ExceptionCheck();
    clearException(env);
    return ok;
  }

  bool call(jmethodID method, std::string_view first, std::string_view second,
            std::string_view third) noexcept {
    if (!open_.load(std::memory_order_acquire)) return false;
    EnvScope scope(vm_);
    JNIEnv* env = scope.get();
    if (!env) return false;
    jstring a = javaString(env, first);
    jstring b = javaString(env, second);
    jstring c = javaString(env, third);
    env->CallVoidMethod(bridge_, method, a, b, c);
    env->DeleteLocalRef(a);
    env->DeleteLocalRef(b);
    env->DeleteLocalRef(c);
    const bool ok = !env->ExceptionCheck();
    clearException(env);
    return ok;
  }

  bool callPresent(std::string_view request, std::string_view target,
                   std::string_view source, bool push) noexcept {
    if (!open_.load(std::memory_order_acquire)) return false;
    EnvScope scope(vm_);
    JNIEnv* env = scope.get();
    if (!env) return false;
    jstring a = javaString(env, request);
    jstring b = javaString(env, target);
    jstring c = push ? javaString(env, source) : nullptr;
    env->CallVoidMethod(bridge_, post_present_, a, b, c,
                        static_cast<jboolean>(push));
    env->DeleteLocalRef(a);
    env->DeleteLocalRef(b);
    if (c) env->DeleteLocalRef(c);
    const bool ok = !env->ExceptionCheck();
    clearException(env);
    return ok;
  }

  bool callVisibility(std::string_view request, std::string_view surface,
                      bool visible) noexcept {
    if (!open_.load(std::memory_order_acquire)) return false;
    EnvScope scope(vm_);
    JNIEnv* env = scope.get();
    if (!env) return false;
    jstring a = javaString(env, request);
    jstring b = javaString(env, surface);
    env->CallVoidMethod(bridge_, post_visibility_, a, b,
                        static_cast<jboolean>(visible));
    env->DeleteLocalRef(a);
    env->DeleteLocalRef(b);
    const bool ok = !env->ExceptionCheck();
    clearException(env);
    return ok;
  }

  jobject makeOperation(JNIEnv* env,
                        const core::render::MountOperation& operation) noexcept {
    int kind = -1;
    std::string node_id;
    std::string parent_id;
    int component = 0;
    std::string name;
    int value_kind = 0;
    bool boolean_value = false;
    double number_value = 0;
    std::string string_value;
    float x = 0, y = 0, width = 0, height = 0;
    int index = 0;
    std::visit([&](const auto& value) {
      using Value = std::decay_t<decltype(value)>;
      node_id = value.node_id.wire();
      if constexpr (std::is_same_v<Value, core::render::CreateHost>) {
        kind = 0;
        component = static_cast<int>(value.type);
      } else if constexpr (std::is_same_v<Value, core::render::SetHostProp>) {
        kind = 1;
        name = value.name;
        if (const auto* boolean = std::get_if<bool>(&value.value)) {
          value_kind = 1;
          boolean_value = *boolean;
        } else if (const auto* number = std::get_if<double>(&value.value)) {
          value_kind = 2;
          number_value = *number;
        } else {
          value_kind = 3;
          string_value = std::get<std::string>(value.value);
        }
      } else if constexpr (std::is_same_v<Value, core::render::SetHostLayout>) {
        kind = 2;
        x = static_cast<float>(value.rect.x);
        y = static_cast<float>(value.rect.y);
        width = static_cast<float>(value.rect.width);
        height = static_cast<float>(value.rect.height);
      } else {
        kind = 3;
        parent_id = value.parent_node_id.wire();
        index = static_cast<int>(value.index);
      }
    }, operation);
    jstring node = javaString(env, node_id);
    jstring parent = parent_id.empty() ? nullptr : javaString(env, parent_id);
    jstring property = name.empty() ? nullptr : javaString(env, name);
    jstring text = value_kind == 3 ? javaString(env, string_value) : nullptr;
    jobject result = env->NewObject(
        operation_class_, operation_constructor_, static_cast<jint>(kind), node,
        parent, static_cast<jint>(component), property,
        static_cast<jint>(value_kind), static_cast<jboolean>(boolean_value),
        static_cast<jdouble>(number_value), text, x, y, width, height,
        static_cast<jint>(index));
    env->DeleteLocalRef(node);
    if (parent) env->DeleteLocalRef(parent);
    if (property) env->DeleteLocalRef(property);
    if (text) env->DeleteLocalRef(text);
    return result;
  }

  JavaVM* vm_{nullptr};
  jobject bridge_{nullptr};
  jclass bridge_class_{nullptr};
  jclass operation_class_{nullptr};
  jclass transaction_class_{nullptr};
  jmethodID post_create_{nullptr};
  jmethodID post_present_{nullptr};
  jmethodID post_visibility_{nullptr};
  jmethodID post_close_{nullptr};
  jmethodID post_destroy_{nullptr};
  jmethodID post_mount_{nullptr};
  jmethodID started_{nullptr};
  jmethodID failed_{nullptr};
  jmethodID stopped_{nullptr};
  jmethodID operation_constructor_{nullptr};
  jmethodID transaction_constructor_{nullptr};
  std::atomic<bool> open_{true};
  std::atomic<std::size_t> pending_{0};
};

struct Session final {
  std::shared_ptr<JniGateway> gateway;
  std::shared_ptr<RuntimeSpine> runtime;
};

std::mutex g_sessions_mutex;
std::map<jlong, std::shared_ptr<Session>> g_sessions;
std::atomic<jlong> g_next_session{1};

std::shared_ptr<Session> session(jlong handle) noexcept {
  std::lock_guard lock(g_sessions_mutex);
  auto found = g_sessions.find(handle);
  return found == g_sessions.end() ? nullptr : found->second;
}

}  // namespace
}  // namespace quickapp::android

using quickapp::android::RuntimeSpine;

extern "C" JNIEXPORT jlong JNICALL
Java_dev_quickapp_kit_android_NativeGateway_create(
    JNIEnv* env, jclass, jobject bridge, jfloat width, jfloat height) {
  auto gateway = quickapp::android::JniGateway::create(env, bridge);
  if (!gateway) return 0;
  auto runtime = RuntimeSpine::create(gateway, width, height);
  if (!runtime) return 0;
  auto value = std::make_shared<quickapp::android::Session>();
  value->gateway = std::move(gateway);
  value->runtime = std::move(runtime);
  const jlong handle = quickapp::android::g_next_session.fetch_add(1);
  std::lock_guard lock(quickapp::android::g_sessions_mutex);
  quickapp::android::g_sessions.emplace(handle, std::move(value));
  return handle;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_quickapp_kit_android_NativeGateway_start(
    JNIEnv* env, jclass, jlong handle, jstring path) {
  auto value = quickapp::android::session(handle);
  if (value) value->runtime->start(quickapp::android::string(env, path));
}

extern "C" JNIEXPORT void JNICALL
Java_dev_quickapp_kit_android_NativeGateway_dispatchClick(
    JNIEnv* env, jclass, jlong handle, jstring surface, jstring node,
    jlong timestamp) {
  auto value = quickapp::android::session(handle);
  if (value) value->runtime->dispatchClick(
      quickapp::android::string(env, surface), quickapp::android::string(env, node),
      static_cast<std::uint64_t>(timestamp));
}

extern "C" JNIEXPORT void JNICALL
Java_dev_quickapp_kit_android_NativeGateway_completeSurface(
    JNIEnv* env, jclass, jlong handle, jstring request, jint kind,
    jstring target, jstring source, jstring reveal, jint visibility,
    jboolean completed, jstring error_code, jstring error_message) {
  auto value = quickapp::android::session(handle);
  if (value) value->runtime->acceptSurfaceResult(
      quickapp::android::string(env, request), kind,
      quickapp::android::string(env, target),
      quickapp::android::optionalString(env, source),
      quickapp::android::optionalString(env, reveal), visibility,
      completed == JNI_TRUE, quickapp::android::optionalString(env, error_code),
      quickapp::android::optionalString(env, error_message));
}

extern "C" JNIEXPORT void JNICALL
Java_dev_quickapp_kit_android_NativeGateway_completeMount(
    JNIEnv* env, jclass, jlong handle, jstring surface, jlong revision,
    jstring attempt, jstring source, jboolean mounted, jstring error_code,
    jstring error_message) {
  auto value = quickapp::android::session(handle);
  if (value) value->runtime->acceptMountResult(
      quickapp::android::string(env, surface),
      static_cast<std::uint64_t>(revision),
      quickapp::android::string(env, attempt),
      quickapp::android::string(env, source), mounted == JNI_TRUE,
      quickapp::android::optionalString(env, error_code),
      quickapp::android::optionalString(env, error_message));
}

extern "C" JNIEXPORT void JNICALL
Java_dev_quickapp_kit_android_NativeGateway_destroy(
    JNIEnv*, jclass, jlong handle) {
  std::shared_ptr<quickapp::android::Session> value;
  {
    std::lock_guard lock(quickapp::android::g_sessions_mutex);
    auto found = quickapp::android::g_sessions.find(handle);
    if (found == quickapp::android::g_sessions.end()) return;
    value = std::move(found->second);
    quickapp::android::g_sessions.erase(found);
  }
  value->runtime->destroy();
}
