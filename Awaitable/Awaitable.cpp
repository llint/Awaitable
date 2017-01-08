// Awaitable.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Awaitable.h"

#include <iostream>

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

    auto x = co_await named_counter("x");
    std::cout << "### after co_await named_counter(x): " << x << std::endl;

    auto y = co_await named_counter("y");
    std::cout << "### after co_await named_counter(y): " << y << std::endl;
}

int main()
{
    test();
    executor::singleton().loop();
    return 0;
}

