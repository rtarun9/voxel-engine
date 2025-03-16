#pragma once

class thread_pool_t
{
  public:
    explicit thread_pool_t(const size_t num_threads = std::thread::hardware_concurrency())
    {
        for (size_t i = 0; i < num_threads; i++)
        {
            m_threads.emplace_back([this]() {
                while (!m_stop_processing_tasks)
                {
                    std::function<void()> curr_func = {};

                    std::unique_lock<std::mutex> lock(m_task_queue_mutex);

                    // Wait until task queue is not empty.
                    m_task_queue_cv.wait(lock, [this]() { return !m_task_queue.empty() || m_stop_processing_tasks; });
                    if (m_stop_processing_tasks)
                    {
                        return;
                    }

                    curr_func = std::move(m_task_queue.front());
                    m_task_queue.pop();

                    m_task_queue_cv.notify_one();

                    curr_func();
                }
            });
        }
    }

    ~thread_pool_t()
    {
        m_stop_processing_tasks = true;
        m_task_queue_cv.notify_all();

        for (auto &thread : m_threads)
        {
            thread.join();
        }
    }

    size_t get_thread_count() const
    {
        return m_threads.size();
    }

    size_t get_tasks_queued() const
    {
        return m_task_queue.size();
    }

    template <typename F, typename... Args>
    auto add_to_task_queue(F &&func, Args &&...args) -> std::future<decltype(func(args...))>
    {
        ZoneScoped;
        // The task queue only accepts function's that takes no arguments.
        // For this, bind the func and args together and create a wrapper function.
        std::function<decltype(func(args...))()> wrapper_func =
            std::bind(std::forward<F>(func), std::forward<Args>(args)...);

        // Wrap the wrapper func in a packaged task so that it can be invoked in async fashion.
        // Wrap the packaged task in a shared pointer to lifetime of packaged task will be handled automatically.
        std::shared_ptr<std::packaged_task<decltype(func(args...))()>> shared_ptr_of_packaged_task =
            std::make_shared<std::packaged_task<decltype(func(args...))()>>(wrapper_func);

        std::future<decltype(func(args...))> future = shared_ptr_of_packaged_task->get_future();

        std::unique_lock<std::mutex> lock(m_task_queue_mutex);
        m_task_queue.push([shared_ptr_of_packaged_task]() {
            ZoneScoped;
            (*shared_ptr_of_packaged_task)();
        });

        m_task_queue_cv.notify_one();

        return future;
    }

  private:
    thread_pool_t(const thread_pool_t &other) = delete;
    thread_pool_t &operator=(const thread_pool_t &other) = delete;

    thread_pool_t(thread_pool_t &&other) = delete;
    thread_pool_t &operator=(thread_pool_t &&other) = delete;

  private:
    std::vector<std::thread> m_threads{};
    std::queue<std::function<void()>> m_task_queue{};

    std::mutex m_task_queue_mutex{};
    std::condition_variable m_task_queue_cv{};
    std::atomic<b32> m_stop_processing_tasks{false};
};