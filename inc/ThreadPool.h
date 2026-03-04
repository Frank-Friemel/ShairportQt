#pragma once
#include <functional>
#include <assert.h>
#include <algorithm>
#include <mutex>
#include <thread>
#include <list>
#include "libutils.h"

class ThreadPool
{
public:
    ThreadPool(size_t threadCount = 1, bool preempt = true) noexcept
        : m_threadCount{ threadCount }
        , m_preempt{ preempt }

    {
        assert(m_threadCount > 0);
    }

    ~ThreadPool()
    {
        clear(m_preempt);
    }

    template<class J>
    void emplace_back(J&& job)
    {
        std::list<std::thread> inactiveThreads;
        const ScopeContext joinInactiveThreads
        {
            [&inactiveThreads]() {
                for (auto& t : inactiveThreads)
                    if (t.joinable())
                    {
                        t.join();
                    }
            }
        };

        const std::lock_guard<std::mutex> sync(m_mtx);

        for (auto t = m_threads.begin(); t != m_threads.end();)
        {
            if (t->isWorking)
            {
                ++t;
            }
            else
            {
                try
                {
                    auto& inactiveThread = inactiveThreads.emplace_back();
                    inactiveThread = std::move(t->thread);
                }
                catch (...)
                {
                    if (t->thread.joinable())
                    {
                        t->thread.join();
                    }
                }
                t = m_threads.erase(t);
            }
        }

        if (m_threads.size() < m_threadCount)
        {
            auto& t = m_threads.emplace_back();

            t.thread = std::thread(
                [this, &isWorking = t.isWorking]() -> void {

                    for (;;)
                    {
                        std::function<void()> job;

                        {
                            const std::lock_guard<std::mutex> sync{ m_mtx };

                            if (m_jobs.empty())
                            {
                                isWorking = false;
                                return;
                            }

                            job = std::move(m_jobs.front());
                            m_jobs.pop_front();
                        }

                        try
                        {
                            job();
                        }
                        catch (...)
                        {
                        }
                    }
                } 
            );
            t.isWorking = true;
        }
        m_jobs.emplace_back(std::forward<J>(job));
    }

    size_t threads() const noexcept
    {
        const std::lock_guard<std::mutex> sync(m_mtx);
        return std::count_if(m_threads.begin(), m_threads.end(), [](const auto& t) { return t.isWorking; });
    }

    size_t size() const noexcept
    {
        const std::lock_guard<std::mutex> sync(m_mtx);
        return m_jobs.size();
    }

    void clear(bool preempt = true) noexcept
    {
        std::list<std::function<void()>> jobs;
        std::unique_lock<std::mutex> sync{ m_mtx };

        if (preempt)
        {
            std::swap(jobs, m_jobs);
        }

        while (!m_threads.empty())
        {
            auto t = std::move(m_threads.begin()->thread);
            sync.unlock();

            if (t.joinable())
            {
                t.join();
            }
            sync.lock();
            m_threads.erase(m_threads.cbegin());
        }
        assert(m_jobs.empty());
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    class Thread
    {
    public:
        bool isWorking{ false }; 
        std::thread thread; 
    };

    mutable std::mutex                  m_mtx;
    const size_t                        m_threadCount;
    const bool                          m_preempt;
    std::list<std::function<void()>>    m_jobs;
    std::list<Thread>                   m_threads;
};