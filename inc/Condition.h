#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <assert.h>
#include "ICondition.h"

#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif

class Condition
    : public ICondition
{
public:
    enum class mode
    {
        one,
        all
    };

    Condition() = default;
    Condition(const Condition&) = delete;
    Condition& operator=(const Condition&) = delete;

    template<class _Cond>
    std::cv_status WaitAndLock(std::unique_lock<std::mutex>& sync, _Cond _cond, uint32_t ms = INFINITE)
    {
        assert(sync.owns_lock());

        if (_cond())
        {
            return std::cv_status::no_timeout;
        }

        for (;;)
        {
            const auto start = std::chrono::steady_clock::now();

            if (ms == INFINITE)
            {
                m_cv.wait(sync);
            }
            else
            {
                if (std::cv_status::timeout == m_cv.wait_for(sync, std::chrono::milliseconds(ms)))
                {
                    assert(sync.owns_lock());
                    return std::cv_status::timeout;
                }
            }
            assert(sync.owns_lock());

            if (_cond())
            {
                break;
            }
            if (ms != INFINITE)
            {
                const auto stop = std::chrono::steady_clock::now();
                const uint32_t diff = static_cast<uint32_t>((double)(std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()) / (double)1000);

                if (ms > diff)
                {
                    ms -= diff;
                }
                else
                {
                    ms = 0;
                }
            }
        }
        return std::cv_status::no_timeout;
    }

    std::cv_status WaitAndLock(std::unique_lock<std::mutex>& sync, uint32_t ms = INFINITE)
    {
        assert(sync.owns_lock());

        if (ms == INFINITE)
        {
            m_cv.wait(sync);
            assert(sync.owns_lock());
            return std::cv_status::no_timeout;
        }
        const std::cv_status cv = m_cv.wait_for(sync, std::chrono::milliseconds(ms));
 
        assert(sync.owns_lock());

        return cv;
    }
    
    void NotifyAndUnlock(std::unique_lock<std::mutex>& sync, const mode m = mode::one) noexcept
    {
        assert(sync.owns_lock());

        if (m == mode::one)
        {
            m_cv.notify_one();
        }
        else
        {
            m_cv.notify_all();
        }
        sync.unlock();
    }

    void NotifyAll() noexcept override
    {
        m_cv.notify_all();
    }

    void NotifyOne() noexcept override
    {
        m_cv.notify_one();
    }
    
private:
    std::condition_variable m_cv;
};