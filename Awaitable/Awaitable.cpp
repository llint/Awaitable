// Awaitable.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Awaitable.h"

#include <iostream>

using namespace pi;

nawaitable set_ready_after_timeout(awaitable<int>::proxy awtbl, std::chrono::high_resolution_clock::duration timeout)
{
    co_await timeout; // timed wait

    awtbl.set_ready(123);
}

nawaitable set_exception_after_timeout(awaitable<int>::proxy awtbl, std::chrono::high_resolution_clock::duration timeout)
{
    co_await timeout;

    awtbl.set_exception(std::make_exception_ptr(std::exception()));
}

awaitable<int> named_counter(std::string name)
{
    std::cout << "counter(" << name << ") resumed #" << 0 << std::endl;

    co_await 5s; // timed wait
    std::cout << "counter(" << name << ") resumed #" << 1 << std::endl;

    int i = co_await awaitable<int>{}; // yield, returns the default value
    std::cout << "counter(" << name << ") resumed #" << 2 << " ### " << i << std::endl;

    {
        auto awtbl = awaitable<int>{ true }; // suspend, and returns the value from somewhere else
        set_ready_after_timeout(awtbl.get_proxy(), 3s);
        auto x = co_await awtbl;
        std::cout << "counter(" << name << ") resumed #" << 3 << " ### " << x << std::endl;
    }

    try
    {
        auto awtbl = awaitable<int>{ true }; // suspend, and returns the value from somewhere else
        set_exception_after_timeout(awtbl.get_proxy(), 3s);
        auto x = co_await awtbl;
        std::cout << "counter(" << name << ") resumed #" << 4 << " ### " << x << std::endl;
    }
    catch (std::exception e)
    {
        std::cout << "### caught exception" << std::endl;
    }

    co_return 42;
}

awaitable<void> test_exception()
{
    co_await 0s;

    throw 0;

    std::cout << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" << std::endl;
}

nawaitable test()
{
    try
    {
        co_await test_exception();
    }
    catch (...)
    {
        std::cout << "caught exception" << std::endl;
    }

    {
        // NB: it doesn't make sense to chain temporary awaitables with operator||, because the return value must be referencing one of the input awaitables
        auto a1 = awaitable<int>{ 3s };
        auto a2 = awaitable<int>{ 4s };
        auto a3 = awaitable<int>{ 5s };
        auto a4 = awaitable<int>{ 6s };
        {
            auto ar = co_await((a2 || a3) || (a1 || a4));
            assert(ar == a1);
            std::cout << "co_await (a1 || a2)" << std::endl;
        }
    }

    // NB: what happens if we do: co_await (a1 || a2 || a1 || a2)?
    // in the current implmentation, the second coroutine that awaits the same awaitable which was awaited by another coroutine already
    // so when the awaitable is awaited, only the most recent awaiter would be resumed, the previous one would never be awaken up anymore!
    // TODO: we might want to use a hashset to store all the awaiters of the same awaitable in the promise_type, so when the awaitable is
    // awaken, all the awaiters would be resumed

    {
        auto a1 = awaitable<void>{ 5s };
        auto a2 = awaitable<void>{ 4s };
        std::deque<awaitable<void>::ref> as{ a1, a2 };
        auto ar = co_await awaitable<void>::when_any(as);
        assert(ar == a2);
        std::cout << "co_await awaitable<void>::when_any(as)" << std::endl;
    }

    {
        auto a1 = awaitable<int>{ 3s };
        auto a2 = awaitable<int>{ 4s };
        auto a3 = awaitable<int>{ 5s };
        co_await (a1 && a2);
        co_await (awaitable<int>{3s} && a3);
        std::cout << "co_await (a1 && a2)" << std::endl;
    }

    {
        auto a1 = awaitable<void>{ 5s };
        auto a2 = awaitable<void>{ 4s };
        std::deque<awaitable<void>::ref> as{ a1, a2 };
        co_await awaitable<void>::when_all(as);
        std::cout << "co_await awaitable<void>::when_all(as)" << std::endl;
    }

    auto x = co_await named_counter("x");
    std::cout << "### after co_await named_counter(x): " << x << std::endl;

    auto y = co_await named_counter("y");
    std::cout << "### after co_await named_counter(y): " << y << std::endl;
}

nawaitable cancel_after_timeout(cancellation& source, std::chrono::high_resolution_clock::duration timeout)
{
    co_await timeout;

    source.fire();
}

nawaitable test_cancellation_1(cancellation::token token)
{
    auto awtbl = awaitable<int>{ true }; // suspend, and returns the value from somewhere else

    // NB: I rely on the fact that awtbl stays on the stack, so I can capture it by reference
    token.register_action([&awtbl] { awtbl.set_exception(std::make_exception_ptr(std::exception())); });

    try
    {
        auto x = co_await awtbl;
    }
    catch (std::exception e)
    {
        std::cout << "test_cancellation_1: canceled!" << std::endl;
    }
}

nawaitable test_cancellation_2(cancellation::token token)
{
    auto awtbl = awaitable<void>{ 4s };

    token.register_action([&awtbl] { awtbl.set_exception(std::make_exception_ptr(std::exception())); });

    try
    {
        co_await awtbl;
    }
    catch (std::exception e)
    {
        std::cout << "test_cancellation_2: canceled!" << std::endl;
    }
}

int main()
{
    cancellation source;
    cancel_after_timeout(source, 3s);
    test_cancellation_1(source.get_token());
    test_cancellation_2(source.get_token());

    test();

    executor::singleton().loop();
    return 0;
}

