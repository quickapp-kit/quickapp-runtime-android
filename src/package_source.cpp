#include "quickapp/android/package_source.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace quickapp::android {
namespace {

RuntimeError ioError(std::string message) {
  return error(ErrorCode::kPackageIoError, std::move(message), true);
}

bool validRange(std::uint64_t size, std::uint64_t offset,
                std::size_t length) {
  if (offset > size) {
    return false;
  }
  const auto remaining = size - offset;
  return length <= remaining;
}

ImmutableBytes copyRange(const std::vector<std::byte>& bytes,
                         std::uint64_t offset, std::size_t length) {
  auto result = std::make_shared<std::vector<std::byte>>();
  result->reserve(length);
  const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
  result->insert(result->end(), begin,
                 begin + static_cast<std::ptrdiff_t>(length));
  return result;
}

}  // namespace

struct AsyncPackageSource::State {
  State(std::shared_ptr<PackageBackend> sourceBackend, Executor& io,
        Executor& core)
      : backend(std::move(sourceBackend)),
        packageSize(backend->size()),
        ioExecutor(io),
        coreExecutor(core) {}

  std::mutex mutex;
  std::shared_ptr<PackageBackend> backend;
  const std::uint64_t packageSize;
  Executor& ioExecutor;
  Executor& coreExecutor;
  bool closed = false;
};

AsyncPackageSource::AsyncPackageSource(
    std::shared_ptr<PackageBackend> backend, Executor& ioExecutor,
    Executor& coreExecutor)
    : state_(std::make_shared<State>(std::move(backend), ioExecutor,
                                     coreExecutor)) {}

AsyncPackageSource::~AsyncPackageSource() { close(); }

std::uint64_t AsyncPackageSource::size() const noexcept {
  return state_->packageSize;
}

void AsyncPackageSource::readAt(std::uint64_t offset, std::size_t length,
                                ReadCompletion completion) {
  auto state = state_;
  {
    std::lock_guard lock(state->mutex);
    if (state->closed || !validRange(state->packageSize, offset, length)) {
      state->coreExecutor.post(
          [completion = std::move(completion)]() mutable {
            completion(Result<ImmutableBytes>::failure(
                ioError("package read is closed or out of range")));
          });
      return;
    }
  }

  state->ioExecutor.post(
      [state, offset, length, completion = std::move(completion)]() mutable {
        Result<ImmutableBytes> result =
            Result<ImmutableBytes>::failure(ioError("package source closed"));
        {
          std::lock_guard lock(state->mutex);
          if (!state->closed) {
            result = state->backend->read(offset, length);
          }
        }
        state->coreExecutor.post(
            [completion = std::move(completion),
             result = std::move(result)]() mutable {
              completion(std::move(result));
            });
      });
}

void AsyncPackageSource::close() noexcept {
  auto state = state_;
  std::lock_guard lock(state->mutex);
  if (state->closed) {
    return;
  }
  state->closed = true;
  state->backend->close();
}

MemoryPackageBackend::MemoryPackageBackend(std::span<const std::byte> bytes)
    : bytes_(bytes.begin(), bytes.end()) {}

std::uint64_t MemoryPackageBackend::size() const noexcept {
  std::lock_guard lock(mutex_);
  return bytes_.size();
}

Result<ImmutableBytes> MemoryPackageBackend::read(std::uint64_t offset,
                                                  std::size_t length) {
  std::lock_guard lock(mutex_);
  if (closed_ || !validRange(bytes_.size(), offset, length)) {
    return Result<ImmutableBytes>::failure(ioError("memory package read failed"));
  }
  return Result<ImmutableBytes>::success(copyRange(bytes_, offset, length));
}

void MemoryPackageBackend::close() noexcept {
  std::lock_guard lock(mutex_);
  closed_ = true;
}

FilePackageBackend::FilePackageBackend(int fileDescriptor, std::uint64_t size)
    : fileDescriptor_(fileDescriptor), size_(size) {}

FilePackageBackend::~FilePackageBackend() { close(); }

Result<std::shared_ptr<FilePackageBackend>> FilePackageBackend::open(
    const std::filesystem::path& path) {
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) {
    const auto code = (errno == ENOENT || errno == ENOTDIR)
                          ? ErrorCode::kPackageNotFound
                          : ErrorCode::kPackageIoError;
    return Result<std::shared_ptr<FilePackageBackend>>::failure(
        error(code, "cannot open package file", code == ErrorCode::kPackageIoError));
  }
  struct stat fileStat {};
  if (::fstat(descriptor, &fileStat) != 0 || !S_ISREG(fileStat.st_mode) ||
      fileStat.st_size < 0) {
    ::close(descriptor);
    return Result<std::shared_ptr<FilePackageBackend>>::failure(
        ioError("cannot inspect package file"));
  }
  return Result<std::shared_ptr<FilePackageBackend>>::success(
      std::shared_ptr<FilePackageBackend>(new FilePackageBackend(
          descriptor, static_cast<std::uint64_t>(fileStat.st_size))));
}

std::uint64_t FilePackageBackend::size() const noexcept { return size_; }

Result<ImmutableBytes> FilePackageBackend::read(std::uint64_t offset,
                                                std::size_t length) {
  std::lock_guard lock(mutex_);
  if (closed_ || fileDescriptor_ < 0 ||
      !validRange(size_, offset, length) ||
      offset > static_cast<std::uint64_t>(
                   std::numeric_limits<off_t>::max()) ||
      length > static_cast<std::size_t>(
                   std::numeric_limits<ssize_t>::max())) {
    return Result<ImmutableBytes>::failure(ioError("file package read failed"));
  }
  auto mutableBytes = std::make_shared<std::vector<std::byte>>(length);
  std::size_t total = 0;
  while (total < length) {
    const auto readCount =
        ::pread(fileDescriptor_, mutableBytes->data() + total, length - total,
                static_cast<off_t>(offset + total));
    if (readCount < 0 && errno == EINTR) {
      continue;
    }
    if (readCount <= 0) {
      return Result<ImmutableBytes>::failure(
          ioError("short package file read"));
    }
    total += static_cast<std::size_t>(readCount);
  }
  return Result<ImmutableBytes>::success(mutableBytes);
}

void FilePackageBackend::close() noexcept {
  std::lock_guard lock(mutex_);
  if (fileDescriptor_ >= 0) {
    ::close(fileDescriptor_);
    fileDescriptor_ = -1;
  }
  closed_ = true;
}

AssetPackageBackend::AssetPackageBackend(
    std::shared_ptr<AssetReader> reader)
    : reader_(std::move(reader)) {}

std::uint64_t AssetPackageBackend::size() const noexcept {
  return reader_->size();
}

Result<ImmutableBytes> AssetPackageBackend::read(std::uint64_t offset,
                                                 std::size_t length) {
  return reader_->read(offset, length);
}

void AssetPackageBackend::close() noexcept { reader_->close(); }

}  // namespace quickapp::android
