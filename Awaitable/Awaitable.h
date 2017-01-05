#pragma once

// ResumableThing.cpp : Defines the entry point for the console application.
//

#include <map>
#include <queue>
#include <chrono>
#include <string>
#include <experimental/coroutine>

using namespace std::chrono;
using namespace std::experimental;

struct executor
{
    static auto& ready_coros()
    {
        static std::queue<coroutine_handle<>> s_ready_coros;
        return s_ready_coros;
    }

    static auto& timed_wait_coros()
    {
        static std::multimap<std::chrono::high_resolution_clock::time_point, coroutine_handle<>> s_timed_wait_coros;
        return s_timed_wait_coros;
    }

    static auto& num_outstanding_coros()
    {
        static int s_num_outstanding_coros = 0;
        return s_num_outstanding_coros;
    }

    static bool tick()
    {
        if (!ready_coros().empty() || !timed_wait_coros().empty() || num_outstanding_coros() > 0)
        {
            if (!ready_coros().empty())
            {
                auto coro = ready_coros().front();
                ready_coros().pop();

                coro.resume();
            }

            while (!timed_wait_coros().empty())
            {
                auto it = timed_wait_coros().begin();
                if (std::chrono::high_resolution_clock::now() < it->first)
                    break;

                ready_coros().push(it->second);
                timed_wait_coros().erase(it);
            }

            return true;
        }

        return false;
    }

    static void loop()
    {
        while (tick())
            ;
    }

};

template <typename T>
class awaitable
{
public:
    typedef std::reference_wrapper<awaitable> ref;

    awaitable() = default;
    ~awaitable() = default;

    explicit awaitable(bool suspend) : _suspend(suspend) {}
    explicit awaitable(std::chrono::high_resolution_clock::duration timeout) : _timeout(timeout) {}

    awaitable(awaitable const&) = delete;
    awaitable& operator=(awaitable const&) = delete;

    // NB: awaitable_completion_source
    awaitable(awaitable&& other)
        : _coroutine(other._coroutine)
        , _ready(other._ready)
        , _suspend(other._suspend)
        , _awaiter_coro(other._awaiter_coro)
        , _timeout(other._timeout)
    {
        other._coroutine = nullptr;
        other._awaiter_coro = nullptr;
    }

    struct promise_type_void
    {
        void return_void()
        {
        }

        void get_value()
        {
        }
    };

    struct promise_type_value
    {
        T value = T{};

        void return_value(T&& value_)
        {
            value = std::move(value_);
        }

        T get_value()
        {
            return value;
        }
    };

    struct promise_type : std::conditional<std::is_same<T, void>::value, promise_type_void, promise_type_value>::type
    {
        awaitable get_return_object()
        {
            return awaitable(coroutine_handle<promise_type>::from_promise(*this));
        }

        auto initial_suspend()
        {
            // NB: we want the coroutine to run until the first actual suspension point, unless explicitly requested to suspend
            return suspend_never{};
        }

        coroutine_handle<> _awaiter_coro = nullptr;

        auto final_suspend()
        {
            if (_awaiter_coro)
            {
                executor::ready_coros().push(_awaiter_coro);
                _awaiter_coro = nullptr;
            }
            return suspend_always{}; // NB: if we want to access the return value in await_resume, we need to keep the current coroutine around, even though coro.done() is now true! 
        }

        std::exception_ptr _exp;
        void set_exception(std::exception_ptr exp)
        {
            _exp = std::move(exp);
        }
    };

    bool await_ready() noexcept
    {
        // if I'm enclosing a coroutine, use its status; otherwise, suspend
        return _coroutine ? _coroutine.done() : _ready;
    }

    void await_suspend(coroutine_handle<> awaiter_coro) noexcept
    {
        if (!_coroutine)
        {
            // I'm not enclosing a coroutine while I'm awaited (await resumable_thing{};), add the awaiter's frame
            if (_timeout.count() > 0)
            {
                executor::timed_wait_coros().emplace(std::chrono::high_resolution_clock::now() + _timeout, awaiter_coro);
            }
            else if (_suspend)
            {
                _awaiter_coro = awaiter_coro;
                ++executor::num_outstanding_coros();
            }
            else
            {
                executor::ready_coros().push(awaiter_coro);
            }
        }
        else
        {
            // I'm waiting for some other coroutine to finish, the awaiter's frame can only be queued until my awaited one finishes
            _coroutine.promise()._awaiter_coro = awaiter_coro;
        }
    }

    T await_resume()
    {
        if (_coroutine)
        {
            if (_coroutine.promise()._exp)
            {
                throw _coroutine.promise()._exp;
            }
        }
        else if (_exp)
        {
            throw _exp;
        }

        return _coroutine ? _coroutine.promise().get_value() : _value.get();
    }

    void set_ready()
    {
        if (_awaiter_coro)
        {
            executor::ready_coros().push(_awaiter_coro);
            _awaiter_coro = nullptr;
            --executor::num_outstanding_coros();
        }
        _ready = true;
    }

    template <typename U = T, typename = std::enable_if<!std::is_same<T, void>::value>::type>
    void set_ready(U&& value)
    {
        _value.value = std::move(value);
        set_ready();
    }

    void set_exception(std::exception_ptr exp)
    {
        _exp = std::move(exp);
    }

private:
    struct type_void
    {
        void get() {}
    };

    struct type_value
    {
        T value = T{};
        T get() { return std::move(value); }
    };

    typename std::conditional<std::is_same<T, void>::value, type_void, type_value>::type _value;

    std::exception_ptr _exp;

    explicit awaitable(coroutine_handle<promise_type> coroutine)
        : _coroutine(coroutine) { }

    // the coroutine this awaitable is enclosing; this is created by promise_type::get_return_object
    coroutine_handle<promise_type> _coroutine = nullptr;

    // the awaiter coroutine when this awaitable is a primitive - i.e. it doesn't enclose a coroutine, set only when _coroutine is nullptr!
    coroutine_handle<> _awaiter_coro = nullptr;

    bool _ready = false;
    bool _suspend = false;
    std::chrono::high_resolution_clock::duration _timeout;
};

