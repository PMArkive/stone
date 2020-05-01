// vim: set sts=4 ts=8 sw=4 tw=99 et:
//
// Copyright (C) 2016-2020 David Anderson
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace stone {

class ThreadPool
{
  public:
    ThreadPool(unsigned int n) {
        threads_.resize(n);
        for (size_t i = 0; i < n; i++) {
            threads_[i] = std::make_unique<std::thread>([this, i]() -> void {
                Work(i);
            });
        }
    }

    ~ThreadPool() {
        Stop();
    }

    void Do(std::function<void(ThreadPool*)> fn, std::function<void()> completion = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        work_.emplace_back(std::move(fn), std::move(completion));
        work_cv_.notify_one();
    }

    void RunCompletionTasks() {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            while (completion_.empty()) {
                if (work_.empty() && !in_progress_)
                    return;
                completion_cv_.wait(lock);
            }

            auto fn = std::move(completion_.front());
            completion_.pop_front();
            lock.unlock();

            fn();

            lock.lock();
        }
    }

    void OnComplete(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        completion_.emplace_back(std::move(fn));
        completion_cv_.notify_one();
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        work_cv_.notify_all();

        for (const auto& thread : threads_)
            thread->join();
        threads_.clear();
    }

    size_t NumThreads() const { return threads_.size(); }

  private:
    void Work(unsigned id) {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            while (work_.empty() && !shutdown_)
                work_cv_.wait(lock);

            if (shutdown_)
                break;

            auto p = std::move(work_.front());
            work_.pop_front();
            in_progress_++;
            lock.unlock();

            p.first(this);
            if (p.second)
                OnComplete(std::move(p.second));

            lock.lock();

            in_progress_--;
            completion_cv_.notify_one();
        }
    };

  private:
    bool shutdown_ = false;
    size_t in_progress_ = 0;
    std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable completion_cv_;
    std::vector<std::unique_ptr<std::thread>> threads_;
    std::deque<std::pair<std::function<void(ThreadPool*)>,
                         std::function<void()>>> work_;
    std::deque<std::function<void()>> completion_;
};

} // namespace stone
