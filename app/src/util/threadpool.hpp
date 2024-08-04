#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace fastchess::util {

class ThreadPool {
   public:
    explicit ThreadPool(std::size_t num_threads) : stop_(false) {
        for (std::size_t i = 0; i < num_threads; ++i) workers_.emplace_back([this] { work(); });
    }

    template <class F, class... Args>
    void enqueue(F &&func, Args &&...args) {
        auto task =
            std::make_shared<std::packaged_task<void()>>(std::bind(std::forward<F>(func), std::forward<Args>(args)...));

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) throw std::runtime_error("Error; enqueue on stopped ThreadPool");
            tasks_.emplace([task]() { (*task)(); });
        }

        condition_.notify_one();
    }

    void resize(std::size_t num_threads) {
        if (num_threads == 0) throw std::invalid_argument("Error; ThreadPool::resize() - num_threads cannot be 0");

        if (num_threads == workers_.size()) return;

        kill();

        stop_ = false;

        workers_.clear();
        workers_.resize(num_threads);

        for (std::size_t i = 0; i < num_threads; ++i) workers_.emplace_back([this] { work(); });
    }

    void kill() {
        if (stop_) return;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_  = true;
            tasks_ = {};
        }

        condition_.notify_all();

        for (auto &worker : workers_) {
            if (worker.joinable()) worker.join();
        }

        workers_.clear();
    }

    ~ThreadPool() { kill(); }

    [[nodiscard]] std::size_t queueSize() {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    [[nodiscard]] bool getStop() { return stop_; }

   private:
    void work() {
        while (!stop_) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }

            task();
        }
    }

    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;

    std::atomic_bool stop_;
};

}  // namespace fastchess::util
