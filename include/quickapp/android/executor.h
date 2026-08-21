#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace quickapp::android {

class Executor {
 public:
  virtual ~Executor() = default;
  virtual void post(std::function<void()> task) = 0;
};

class SerialExecutor final : public Executor {
 public:
  SerialExecutor();
  ~SerialExecutor() override;
  SerialExecutor(const SerialExecutor&) = delete;
  SerialExecutor& operator=(const SerialExecutor&) = delete;

  void post(std::function<void()> task) override;

 private:
  void run();
  std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<std::function<void()>> tasks_;
  bool stopping_ = false;
  std::thread worker_;
};

class ManualExecutor final : public Executor {
 public:
  void post(std::function<void()> task) override;
  bool runOne();
  std::size_t runAll();
  std::size_t pending() const;

 private:
  mutable std::mutex mutex_;
  std::queue<std::function<void()>> tasks_;
};

}  // namespace quickapp::android

