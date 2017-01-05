#pragma once

// ResumableThing.cpp : Defines the entry point for the console application.
//

#include <map>
#include <queue>
#include <chrono>
#include <string>
#include <iostream>
#include <experimental/coroutine>

using namespace std::chrono;
using namespace std::experimental;

std::queue<coroutine_handle<>> ready_coros;

std::map<std::chrono::high_resolution_clock::time_point, coroutine_handle<>> suspended_coros;

template <typename T>
class awaitable
{
public:
    awaitable() = default;
    ~awaitable() = default;

    explicit awaitable(bool suspend) : _suspend(suspend) {}

    awaitable(awaitable const&) = delete;
    awaitable& operator=(awaitable const&) = delete;

    // NB: awaitable_completion_source
    awaitable(awaitable&& other)
        : _coroutine(other._coroutine)
        , _ready(other._ready)
        , _suspend(other._suspend)
    {
        other._coroutine = nullptr;
    }

    struct promise_type_void
    {
        void return_void()
        {
            std::cout << "return_void()" << std::endl;
        }

        void generic_return() {}
    };

    struct promise_type_value
    {
        T value = T{};

        void return_value(T value_)
        {
            std::cout << "return_value: " << value_ << std::endl;
            value = value_;
        }

        T generic_return() { return value; }
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
                ready_coros.push(_awaiter_coro);
                _awaiter_coro = nullptr;
            }
            return suspend_always{}; // NB: if we want to access the return value in await_resume, we need to keep the current coroutine around, even though coro.done() is now true! 
        }
    };

    bool await_ready() noexcept
    {
        // if I'm enclosing a coroutine, use its status; otherwise, suspend
        return _coroutine ? _coroutine.done() : false;
    }

    void await_suspend(coroutine_handle<> awaiter_coro) noexcept
    {
        if (!_coroutine)
        {
            // I'm not enclosing a coroutine while I'm awaited (await resumable_thing{};), add the awaiter's frame
            if (_suspend)
            {
                suspended_coros.emplace(std::chrono::high_resolution_clock::now(), awaiter_coro);
            }
            else
            {
                ready_coros.push(awaiter_coro);
            }
        }
        else
        {
            // I'm waiting for some other coroutine to finish, the awaiter's frame can only be queued until my awaited one finishes
            _coroutine.promise()._awaiter_coro = awaiter_coro;
        }
    }

    T await_resume() noexcept
    {
        std::cout << "await_resume" << std::endl;
        return _coroutine.promise().generic_return();
    }

private:
    explicit awaitable(coroutine_handle<promise_type> coroutine)
        : _coroutine(coroutine) { }

    // the coroutine this awaitable is enclosing; this is created by promise_type::get_return_object
    coroutine_handle<promise_type> _coroutine = nullptr;

    // the awaiter coroutine when this awaitable is a primitive - i.e. it doesn't enclose a coroutine, set only when _coroutine is nullptr!
    coroutine_handle<> _awaiter_coro = nullptr;

    bool _ready = false;
    bool _suspend = false;
};

