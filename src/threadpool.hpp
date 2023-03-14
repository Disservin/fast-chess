#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace fast_chess
{

class ThreadPool
{
  public:
    ThreadPool(size_t threads) : stop_(false)
    {
        for (size_t i = 0; i < threads; ++i)
            // Each worker thread runs an infinite loop that waits for tasks_ to be added to the
            // queue
            workers_.emplace_back([this] {
                while (!this->stop_)
                {
                    std::function<void()> task;

                    // Acquire a lock on the queue mutex and wait for a task to be added to the
                    // queue
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex_);
                        this->condition_.wait(
                            lock, [this] { return this->stop_ || !this->tasks_.empty(); });
                        if (this->stop_ && this->tasks_.empty())
                            return;
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    task();
                }
            });
    }

    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args)
        -> std::future<typename std::invoke_result<F, Args...>::type>
    {
        using return_type = typename std::invoke_result<F, Args...>::type;

        // Create a packaged task that wraps the specified function and arguments
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        // Get a future that will eventually hold the result of the task
        std::future<return_type> res = task->get_future();
        {
            // Acquire a lock on the queue mutex and add the task to the queue
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // don't allow enqueueing after stopping the pool
            if (stop_)
                throw std::runtime_error("Warning: enqueue on stopped ThreadPool");

            // Move the task into the queue as a function object

            tasks_.emplace([task]() { (*task)(); });
        }
        // Notify one worker thread that a new task is available
        condition_.notify_one();
        return res;
    }

    void resize(size_t num_threads)
    {
        if (num_threads == 0)
        {
            throw std::invalid_argument("Warning: ThreadPool::resize() - num_threads cannot be 0");
        }

        size_t curr_threads = workers_.size();
        if (num_threads > curr_threads)
        {
            // Add new threads
            for (size_t i = 0; i < num_threads - curr_threads; ++i)
            {
                workers_.emplace_back([this] {
                    while (true)
                    {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(queue_mutex_);
                            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                            if (stop_ && tasks_.empty())
                                return;
                            task = std::move(tasks_.front());
                            tasks_.pop();
                        }

                        task();
                    }
                });
            }
        }
        else if (num_threads < curr_threads)
        {
            // Signal workers_ to exit and join them
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                stop_ = true;
            }
            condition_.notify_all();
            for (auto &worker : workers_)
                worker.join();
            workers_.resize(num_threads);
            stop_ = false;
            for (size_t i = curr_threads; i < num_threads; ++i)
            {
                workers_.emplace_back([this] {
                    while (true)
                    {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(queue_mutex_);
                            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                        }
                    }
                });
            }
        }
    }

    void kill()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            std::queue<std::function<void()>> empty;
            std::swap(tasks_, empty);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto &worker : workers_)
        {
            if (worker.joinable())
            {
                /*is using detach or join here better?
                this gets called when ctrl c happens
                so in order to quickly cleanup i guess detach?
                */
                worker.detach();
            }
        }
        workers_.clear();
    }

    ~ThreadPool()
    {
        kill();
    }

    std::atomic_bool stop_ = false;

  private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex queue_mutex_;
    std::condition_variable condition_;
};

} // namespace fast_chess
