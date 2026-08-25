#include <cstdlib>
#include <iostream>
#include <string>

#include "quickapp/android/feature_provider.h"

namespace {

namespace qc = quickapp::core;
namespace qcf = quickapp::core::feature;

qc::RequestId requestId(const char* wire) {
  return qc::RequestId::parse(wire).value();
}

qc::SurfaceId surfaceId() { return qc::SurfaceId::parse("srf:b4").value(); }

qcf::Request request(const char* request_wire, qcf::ModuleId module,
                     qcf::Method method) {
  return {requestId(request_wire), surfaceId(), module, method, {}, std::nullopt,
          0, std::nullopt, {}, {}, std::nullopt, 0, {}, std::nullopt,
          std::nullopt, std::nullopt};
}

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "b4_feature_provider_failure=" << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  quickapp::android::platform::AndroidFeatureProvider provider;

  auto prompt = request("req:b4-prompt", qcf::ModuleId::kSystemPrompt,
                        qcf::Method::kConfirm);
  prompt.text = "continue";
  auto prompt_result = provider.invoke(prompt);
  expect(prompt_result.status == qcf::Status::kSuccess &&
             prompt_result.confirmed.value_or(false),
         "prompt_completed");

  auto failed_fetch = request("req:b4-failed", qcf::ModuleId::kSystemFetch,
                              qcf::Method::kFetch);
  failed_fetch.url = "local://platform/failure";
  auto failed_result = provider.invoke(failed_fetch);
  expect(failed_result.status == qcf::Status::kFailed, "fetch_failed");

  auto cancelled_fetch = request("req:b4-cancelled", qcf::ModuleId::kSystemFetch,
                                 qcf::Method::kFetch);
  cancelled_fetch.url = "local://platform/cancelled";
  auto cancelled_result = provider.invoke(cancelled_fetch);
  expect(cancelled_result.status == qcf::Status::kCancelled, "fetch_cancelled");
  expect(provider.cancel(cancelled_fetch.request_id, cancelled_fetch.surface_id),
         "cancel_contract");

  auto file = request("req:b4-file", qcf::ModuleId::kSystemFile,
                      qcf::Method::kFileRead);
  file.path = "private/platform-state.txt";
  auto file_result = provider.invoke(file);
  expect(file_result.status == qcf::Status::kSuccess &&
             file_result.file_data.value_or("") == "android-provider-ready",
         "file_private_read");

  auto rejected_file = file;
  rejected_file.request_id = requestId("req:b4-rejected");
  rejected_file.path = "../outside.txt";
  auto rejected_result = provider.invoke(rejected_file);
  expect(rejected_result.status == qcf::Status::kFailed, "file_path_rejected");

  qcf::ModuleRegistry empty_registry;
  auto unsupported = empty_registry.invoke(prompt);
  expect(unsupported.status == qcf::Status::kUnsupported, "missing_provider");

  provider.teardown(surfaceId());
  std::cout << "b4_feature_provider=passed\n";
  return 0;
}
