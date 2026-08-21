#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "quickapp/android/executor.h"
#include "quickapp/android/result.h"

namespace quickapp::android {

using ImmutableBytes = std::shared_ptr<const std::vector<std::byte>>;
using ReadCompletion = std::function<void(Result<ImmutableBytes>)>;

class PackageBackend {
 public:
  virtual ~PackageBackend() = default;
  virtual std::uint64_t size() const noexcept = 0;
  virtual Result<ImmutableBytes> read(std::uint64_t offset,
                                      std::size_t length) = 0;
  virtual void close() noexcept = 0;
};

class PackageSource {
 public:
  virtual ~PackageSource() = default;
  virtual std::uint64_t size() const noexcept = 0;
  virtual void readAt(std::uint64_t offset, std::size_t length,
                      ReadCompletion completion) = 0;
  virtual void close() noexcept = 0;
};

class AsyncPackageSource final : public PackageSource {
 public:
  AsyncPackageSource(std::shared_ptr<PackageBackend> backend,
                     Executor& ioExecutor, Executor& coreExecutor);
  ~AsyncPackageSource() override;

  std::uint64_t size() const noexcept override;
  void readAt(std::uint64_t offset, std::size_t length,
              ReadCompletion completion) override;
  void close() noexcept override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};

class MemoryPackageBackend final : public PackageBackend {
 public:
  explicit MemoryPackageBackend(std::span<const std::byte> bytes);
  std::uint64_t size() const noexcept override;
  Result<ImmutableBytes> read(std::uint64_t offset,
                              std::size_t length) override;
  void close() noexcept override;

 private:
  mutable std::mutex mutex_;
  std::vector<std::byte> bytes_;
  bool closed_ = false;
};

class FilePackageBackend final : public PackageBackend {
 public:
  static Result<std::shared_ptr<FilePackageBackend>> open(
      const std::filesystem::path& path);
  ~FilePackageBackend() override;

  std::uint64_t size() const noexcept override;
  Result<ImmutableBytes> read(std::uint64_t offset,
                              std::size_t length) override;
  void close() noexcept override;

 private:
  FilePackageBackend(int fileDescriptor, std::uint64_t size);
  int fileDescriptor_;
  std::uint64_t size_;
  mutable std::mutex mutex_;
  bool closed_ = false;
};

class AssetReader {
 public:
  virtual ~AssetReader() = default;
  virtual std::uint64_t size() const noexcept = 0;
  virtual Result<ImmutableBytes> read(std::uint64_t offset,
                                      std::size_t length) = 0;
  virtual void close() noexcept = 0;
};

class AssetPackageBackend final : public PackageBackend {
 public:
  explicit AssetPackageBackend(std::shared_ptr<AssetReader> reader);
  std::uint64_t size() const noexcept override;
  Result<ImmutableBytes> read(std::uint64_t offset,
                              std::size_t length) override;
  void close() noexcept override;

 private:
  std::shared_ptr<AssetReader> reader_;
};

}  // namespace quickapp::android
