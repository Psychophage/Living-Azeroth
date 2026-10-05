// PBC Character System changes, 2026-09-30; see NOTICE.md for upstream attribution.
// Dedicated storage/model queues: no std::async destructor can block a game tick.
#ifndef PBC_WORKER_H
#define PBC_WORKER_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>

namespace PBC
{
class Worker
{
public:
    Worker() : _thread([this] { Run(); }) { }
    ~Worker()
    {
        {
            std::lock_guard lock(_mutex);
            _stopping = true;
        }
        _ready.notify_all();
        _thread.join(); // Shutdown only; already queued persistence work drains first.
    }
    Worker(Worker const&) = delete;
    Worker& operator=(Worker const&) = delete;

    template<typename Function>
    auto Submit(Function function) -> std::future<std::invoke_result_t<Function>>
    {
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(function));
        auto future = task->get_future();
        {
            std::lock_guard lock(_mutex);
            _tasks.emplace_back([task] { (*task)(); });
        }
        _ready.notify_one();
        return future;
    }

private:
    void Run()
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock lock(_mutex);
                _ready.wait(lock, [&] { return _stopping || !_tasks.empty(); });
                if (_tasks.empty())
                    return;
                task = std::move(_tasks.front());
                _tasks.pop_front();
            }
            task();
        }
    }
    std::mutex _mutex;
    std::condition_variable _ready;
    std::deque<std::function<void()>> _tasks;
    bool _stopping = false;
    std::thread _thread;
};

template<typename T>
bool Ready(std::future<T> const& future)
{
    return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
}

#endif
