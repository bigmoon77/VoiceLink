#pragma once

#include "storage_system.h"

#include <atomic>
#include <cstdint>
#include <string>

// 想定頻度: 計測対象関数が呼び出されるたびに1回。
template<auto Function>
class call_frequency final
{
public:
    struct entry
    {
        std::wstring name;
        std::uint64_t count;
    };

    // 想定頻度: 計測対象関数の呼び出しごとに1回。
    static void increment() noexcept
    {
        _count.fetch_add(1, std::memory_order_relaxed);
    }

    // 想定頻度: 呼び出し頻度の確認・レポート作成時のみ。
    static std::uint64_t get_count() noexcept
    {
        return _count.load(std::memory_order_relaxed);
    }

    // 想定頻度: 計測値のリセット操作時のみ。
    static std::uint64_t reset() noexcept
    {
        return _count.exchange(0, std::memory_order_relaxed);
    }

    // 想定頻度: レポート作成時のみ。
    static entry make_entry(const wchar_t* name)
    {
        return { name ? name : L"", get_count() };
    }

private:
    inline static std::atomic<std::uint64_t> _count{ 0 };
};

// 想定頻度: 最適化前の計測結果を保存するときのみ。
class call_frequency_report final
{
public:
    // 想定頻度: ユーザーが計測結果を保存するときに1回。
    template<typename... Entries>
    static bool save(const Entries&... entries)
    {
        std::wstring contents = L"function,count\r\n";
        (append_entry(contents, entries), ...);

        const std::wstring directory = storage_system::get_directory_path(
            storage_system::directory::logs, true
        );
        if (directory.empty()) return false;

        SYSTEMTIME time{};
        GetLocalTime(&time);

        wchar_t file_name[96]{};
        swprintf_s(
            file_name,
            L"call_frequency_%04u%02u%02u_%02u%02u%02u_%03u_%lu_%llu.csv",
            time.wYear,
            time.wMonth,
            time.wDay,
            time.wHour,
            time.wMinute,
            time.wSecond,
            time.wMilliseconds,
            GetCurrentProcessId(),
            GetTickCount64()
        );

        const std::wstring path = directory + L"\\" + file_name;
        return storage_system::write_utf8_text_new(path, contents);
    }

private:
    // 想定頻度: レポート作成時にエントリー1件につき1回。
    template<typename Entry>
    static void append_entry(std::wstring& contents, const Entry& value)
    {
        contents += L'"';
        for (const wchar_t character : value.name)
        {
            if (character == L'"') contents += L"\"\"";
            else contents += character;
        }
        contents += L"\",";
        contents += std::to_wstring(value.count);
        contents += L"\r\n";
    }
};
