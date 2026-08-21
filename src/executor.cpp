#include "quickapp/android/executor.h"

#include <stdexcept>
#include <utility>

namespace quickapp::android {

SerialExecutor::SerialExecutor() : worker_([this] { run(); }) {}

SerialExecutor::~SerialExecutor() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  ready_.notify_one();
  worker_.join();
}

void SerialExecutor::post(std::function<void()> task) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) {
      throw std::logic_error("executor is stopping");
    }
    tasks_.push(std::move(task));
  }
  ready_.notify_one();
}

void SerialExecutor::run() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
      if (stopping_ && tasks_.empty()) {
        return;
      }
      task = std::move(tasks_.front());
      tasks_.pop();
    }
    task();
  }
}

void ManualExecutor::post(std::function<void()> task) {
  std::lock_guard lock(mutex_);
  tasks_.push(std::move(task));
}

bool ManualExecutor::runOne() {
  std::function<void()> task;
  {
    std::lock_guard lock(mutex_);
    if (tasks_.empty()) {
      return false;
    }
    task = std::move(tasks_.front());
    tasks_.pop();
  }
  task();
  return true;
}

std::size_t ManualExecutor::runAll() {
  std::size_t count = 0;
  while (runOne()) {
    ++count;
  }
  return count;
}

std::size_t ManualExecutor::pending() const {
  std::lock_guard lock(mutex_);
  return tasks_.size();
}

}  // namespace quickapp::android

