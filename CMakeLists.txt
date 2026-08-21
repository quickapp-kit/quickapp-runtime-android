cmake_minimum_required(VERSION 3.24)
project(quickapp_runtime_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(quickapp_android_host STATIC
  src/composition.cpp
  src/executor.cpp
  src/json.cpp
  src/launch_profile.cpp
  src/package_source.cpp
  src/result.cpp
  src/runtime_host.cpp
)
target_include_directories(quickapp_android_host PUBLIC include)
target_compile_options(quickapp_android_host PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_executable(and_s01_contract_tests tests/and_s01_contract_tests.cpp)
target_link_libraries(and_s01_contract_tests PRIVATE quickapp_android_host)
target_compile_options(and_s01_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME and_s01_contract_tests COMMAND and_s01_contract_tests)

if(ANDROID)
  set(QUICKAPP_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
  set(QUICKAPP_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../quickapp-runtime-core")
  set(QUICKAPP_JS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../quickapp-runtime-js")

  set(QUICKAPP_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(QUICKAPP_JS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${QUICKAPP_CORE_ROOT}"
                   "${CMAKE_CURRENT_BINARY_DIR}/quickapp-runtime-core")
  add_subdirectory("${QUICKAPP_JS_ROOT}"
                   "${CMAKE_CURRENT_BINARY_DIR}/quickapp-runtime-js")

  add_library(quickapp_android_platform STATIC
    src/platform_adapter.cpp
    src/runtime_spine.cpp
  )
  target_include_directories(quickapp_android_platform PUBLIC include)
  target_link_libraries(quickapp_android_platform PUBLIC
    quickapp_android_host
    quickapp_core_package
    quickapp_core_render
    quickapp_js_engine_quickjs
    quickapp_js_engine_api
    quickapp_js_executor
    quickapp_js_module_loader
    quickapp_js_static_facades
    quickapp_js_page_host_control
    quickapp_js_vm_lifecycle
    quickapp_js_alpha_initial_binding
    quickapp_js_alpha_initial_render
    quickapp_js_event
    quickapp_js_alpha_page_stage
  )
  target_compile_options(quickapp_android_platform PRIVATE
    -Wall -Wextra -Wpedantic -Werror)

  add_library(quickapp_android_runtime SHARED src/jni_gateway.cpp)
  target_include_directories(quickapp_android_runtime PRIVATE include)
  target_link_libraries(quickapp_android_runtime PRIVATE
    quickapp_android_platform android log)
  target_compile_options(quickapp_android_runtime PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
