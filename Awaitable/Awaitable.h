#pragma once

// ResumableThing.cpp : Defines the entry point for the console application.
//

#include <map>
#include <queue>
#include <chrono>
#include <string>
#include <experimental/coroutine>

#include <cassert>

using namespace std::chrono;
using namespace std::experimental;

// TODO: maybe better encapsulation? singleton? so we only need to set the singleton instance to thread_local
struct executor
{
    static auto& ready_coros()
    {
        thread_local static std::queue<coroutine_handle<>> s_ready_coros;
        return s_ready_coros;
    }

    static auto& timed_wait_coros()
    {
        thread_local static std::multimap<std::chrono::high_resolution_clock::time_point, coroutine_handle<>> s_timed_wait_coros;
        return s_timed_wait_coros;
    }

    static auto& num_outstanding_coros()
    {
        thread_local static int s_num_outstanding_coros = 0;
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

    awaitable(awaitable&& other)
        : _coroutine(other._coroutine)
        , _ready(other._ready)
        , _suspend(other._suspend)
        , _awaiter_coro(other._awaiter_coro)
        , _timeout(other._timeout)
    {
        other._timeout = 0;
        other._ready = false;
        other._suspend = false;
        other._coroutine = nullptr;
        other._awaiter_coro = nullptr;
    }

    template <typename X>
    struct promise_type_base
    {
        X _value = X{};

        void return_value(X&& value)
        {
            _value = std::move(value);
        }
    };

    template <>
    struct promise_type_base<void>
    {
        void return_void()
        {
        }
    };

    struct promise_type : promise_type_base<T>
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
        // if I'm enclosing a coroutine, use its status; otherwise, suspend if not ready
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

    template <typename X>
    struct value
    {
        X _value = X{};
        X get() { return std::move(_value); }
    };

    template <>
    struct value<void>
    {
        void get() {}
    };

    template <typename X>
    struct save_promise_value
    {
        static void apply(value<X>& v, promise_type& p)
        {
            v._value = std::move(p._value);
        }
    };

    template <>
    struct save_promise_value<void>
    {
        static void apply(value<void>&, promise_type&)
        {
        }
    };

    T await_resume()
    {
        if (_coroutine)
        {
            if (_coroutine.promise()._exp)
            {
                _exp = _coroutine.promise()._exp;
            }
            else
            {
                save_promise_value<T>::apply(_value, _coroutine.promise());
            }

            // the coroutine is finished, but returned from final_suspend (suspend_always), so we get a chance to retrieve any exception or value
            assert(_coroutine.done());
            _coroutine.destroy();
        }

        if (_exp)
        {
            throw _exp;
        }

        return _value.get();
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
        _value._value = std::move(value);
        set_ready();
    }

    void set_exception(std::exception_ptr exp)
    {
        _exp = std::move(exp);
    }

private:
    value<T> _value;

    std::exception_ptr _exp;

    explicit awaitable(coroutine_handle<promise_type> coroutine)
        : _coroutine(coroutine) {}

    // the coroutine this awaitable is enclosing; this is created by promise_type::get_return_object
    coroutine_handle<promise_type> _coroutine = nullptr;

    // the awaiter coroutine when this awaitable is a primitive - i.e. it doesn't enclose a coroutine, set only when _coroutine is nullptr!
    coroutine_handle<> _awaiter_coro = nullptr;

    bool _ready = false;
    bool _suspend = false;
    std::chrono::high_resolution_clock::duration _timeout;
};

auto operator co_await(std::chrono::high_resolution_clock::duration duration)
{
    return awaitable<void>{duration};
}

