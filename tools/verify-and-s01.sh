#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$project_dir/build"
sanitize_dir="$project_dir/build-sanitize"

cmake -S "$project_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure
"$build_dir/and_s01_contract_tests"

cmake -S "$project_dir" -B "$sanitize_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build "$sanitize_dir"
ctest --test-dir "$sanitize_dir" --output-on-failure

if nm -gU "$build_dir/libquickapp_android_host.a" |
  grep -E 'Java_|JNIEnv|SurfaceView|MountTransaction|PlatformInputMessage'
then
  echo "Unexpected later-phase platform symbol found" >&2
  exit 1
fi

echo "AND-S01 verification passed"

