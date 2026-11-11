#pragma once

#include <atomic>
#include <cstdint>

// 想定頻度: 計測対象関数が呼び出されるたびに1回。
template<auto Function>
class call_frequency final
{
public:
    // 想定頻度: 計測対象関数の呼び出しごとに1回。
    static void increment() noexcept
    {
        _count.fetch_add(1, std::memory_order_relaxed);
    }

    // 想定頻度: 呼び出し頻度の確認時のみ。
    static std::uint64_t get_count() noexcept
    {
        return _count.load(std::memory_order_relaxed);
    }

    // 想定頻度: 計測値のリセット操作時のみ。
    static std::uint64_t reset() noexcept
    {
        return _count.exchange(0, std::memory_order_relaxed);
    }

private:
    inline static std::atomic<std::uint64_t> _count{ 0 };
};
