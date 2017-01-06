// Awaitable.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Awaitable.h"

#include <iostream>

awaitable<void> set_ready_after_timeout(awaitable<int>::ref awtb, std::chrono::high_resolution_clock::duration timeout)
{
    co_await awaitable<void>{timeout}; // timed wait

    awtb.get().set_ready(123);
}

awaitable<int> named_counter(std::string name)
{
    std::cout << "counter(" << name << ") resumed #" << 0 << std::endl;

    co_await awaitable<void>{2s}; // timed wait
    std::cout << "counter(" << name << ") resumed #" << 1 << std::endl;

    int i = co_await awaitable<int>{}; // yield, returns the default value
    std::cout << "counter(" << name << ") resumed #" << 2 << " ### " << i << std::endl;

    auto awtb = awaitable<int>{true}; // suspend, and returns the value from somewhere else
    set_ready_after_timeout(awtb, 3s);
    auto x = co_await awtb;
    std::cout << "counter(" << name << ") resumed #" << 3 << " ### " << x << std::endl;

    co_return 42;
}

awaitable<void> test_exception()
{
    co_await awaitable<void>{};

    throw 0;

    std::cout << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" << std::endl;
}

awaitable<void> test()
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
    executor::loop();
    return 0;
}

