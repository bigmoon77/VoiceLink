#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <climits>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

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
#ifdef _DEBUG
        _count.fetch_add(1, std::memory_order_relaxed);
#endif
    }

    // 想定頻度: 呼び出し頻度の確認・レポート作成時のみ。
    static std::uint64_t get_count() noexcept
    {
#ifdef _DEBUG
        return _count.load(std::memory_order_relaxed);
#else
        return 0;
#endif
    }

    // 想定頻度: 計測値のリセット操作時のみ。
    static std::uint64_t reset() noexcept
    {
#ifdef _DEBUG
        return _count.exchange(0, std::memory_order_relaxed);
#else
        return 0;
#endif
    }

    // 想定頻度: レポート作成時のみ。
    static entry make_entry(const wchar_t* name)
    {
#ifdef _DEBUG
        return { name ? name : L"", get_count() };
#else
        return {};
#endif
    }

private:
#ifdef _DEBUG
    inline static std::atomic<std::uint64_t> _count{ 0 };
#endif
};

// 想定頻度: 最適化前の計測結果を保存するときのみ。
class call_frequency_report final
{
public:
    // 想定頻度: ユーザーが計測結果を保存するときに1回。
    template<typename... Entries>
    static bool save(const Entries&... entries)
    {
#ifdef _DEBUG
#ifndef VOICE_LINK_SOLUTION_DIR
#error VOICE_LINK_SOLUTION_DIR must be defined in Debug configurations.
#endif
        std::wstring contents = L"function,count\r\n";
        (append_entry(contents, entries), ...);

        const std::wstring directory =
            VOICE_LINK_SOLUTION_DIR L"call_frequency";
        if (!CreateDirectoryW(directory.c_str(), nullptr))
        {
            if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
            const DWORD attributes = GetFileAttributesW(directory.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                return false;
            }
        }

        SYSTEMTIME time{};
        GetLocalTime(&time);

        static std::atomic<std::uint64_t> file_counter{ 0 };
        const std::uint64_t serial =
            file_counter.fetch_add(1, std::memory_order_relaxed);

        wchar_t file_name[112]{};
        swprintf_s(
            file_name,
            L"call_frequency_%04u%02u%02u_%02u%02u%02u_%03u_%lu_%llu_%llu.csv",
            time.wYear,
            time.wMonth,
            time.wDay,
            time.wHour,
            time.wMinute,
            time.wSecond,
            time.wMilliseconds,
            GetCurrentProcessId(),
            GetTickCount64(),
            serial
        );

        const std::wstring path = directory + L"\\" + file_name;
        std::vector<std::uint8_t> data;
        if (!encode_utf8(contents, data)) return false;

        HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (file == INVALID_HANDLE_VALUE) return false;

        bool success = true;
        std::size_t offset = 0;
        while (offset < data.size())
        {
            const DWORD chunk = static_cast<DWORD>((std::min)(
                data.size() - offset,
                static_cast<std::size_t>(MAXDWORD)
            ));
            DWORD written = 0;
            if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr) ||
                written == 0)
            {
                success = false;
                break;
            }
            offset += written;
        }
        if (success) success = FlushFileBuffers(file) != FALSE;
        CloseHandle(file);
        if (!success) DeleteFileW(path.c_str());
        return success;
#else
        (void)sizeof...(entries);
        return false;
#endif
    }

private:
#ifdef _DEBUG
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

    static bool encode_utf8(
        const std::wstring& text,
        std::vector<std::uint8_t>& data
    )
    {
        if (text.size() > static_cast<std::size_t>(INT_MAX)) return false;
        const int byte_count = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        if (byte_count < 0 || (byte_count == 0 && !text.empty())) return false;

        data = { 0xEF, 0xBB, 0xBF };
        const std::size_t offset = data.size();
        data.resize(offset + static_cast<std::size_t>(byte_count));
        if (byte_count > 0 &&
            WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                text.data(),
                static_cast<int>(text.size()),
                reinterpret_cast<char*>(data.data() + offset),
                byte_count,
                nullptr,
                nullptr
            ) != byte_count)
        {
            return false;
        }
        return true;
    }
#endif
};
