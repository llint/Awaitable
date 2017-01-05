// Awaitable.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Awaitable.h"

awaitable<int> named_counter(std::string name)
{
    std::cout << "counter(" << name << ") resumed #" << 0 << std::endl;
    co_await awaitable<void>{true};
    std::cout << "counter(" << name << ") resumed #" << 1 << std::endl;
    co_await awaitable<void>{};
    std::cout << "counter(" << name << ") resumed #" << 2 << std::endl;
    co_await awaitable<void>{};
    std::cout << "counter(" << name << ") resumed #" << 3 << std::endl;

    co_return 42;
}

awaitable<void> test()
{
    auto x = co_await named_counter("x");
    std::cout << "### after co_await named_counter(x): " << x << std::endl;

    auto y = co_await named_counter("y");
    std::cout << "### after co_await named_counter(y): " << y << std::endl;
}

int main()
{
    test();
    while (!ready_coros.empty() || !suspended_coros.empty())
    {
        if (!ready_coros.empty())
        {
            auto coro = ready_coros.front();
            ready_coros.pop();

            coro.resume();
        }

        while (!suspended_coros.empty())
        {
            auto it = suspended_coros.begin();
            if (std::chrono::high_resolution_clock::now() - it->first < 2s)
                break;

            ready_coros.push(it->second);
            suspended_coros.erase(it);
        }
    }
    return 0;
}

