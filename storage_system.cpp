#include "storage_system.h"

#include <algorithm>
#include <climits>
#include <cwchar>
#include <atomic>
#include <limits>

namespace
{
    std::atomic<unsigned long long> temporary_counter{ 0 };

    // 想定頻度: UTF-8テキスト読み込み1回につき1回。
    bool has_utf8_bom(const std::vector<std::uint8_t>& data)
    {
        return data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF;
    }

    // 想定頻度: UTF-8 テキスト書き込み時。
    bool encode_utf8(const std::wstring& text, std::vector<std::uint8_t>& data, bool bom)
    {
        if (text.size() > static_cast<std::size_t>(INT_MAX)) return false;
        const int byte_count = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            nullptr, 0, nullptr, nullptr
        );
        if (byte_count < 0 || (byte_count == 0 && !text.empty())) return false;
        data.clear();
        if (bom) data.insert(data.end(), { 0xEF, 0xBB, 0xBF });
        const std::size_t offset = data.size();
        data.resize(offset + static_cast<std::size_t>(byte_count));
        if (byte_count > 0 && WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            reinterpret_cast<char*>(data.data() + offset), byte_count, nullptr, nullptr
        ) != byte_count) return false;
        return true;
    }

    // 想定頻度: 一時ファイル作成またはロック名生成時。保存操作1回につき1回。
    std::wstring make_unique_suffix()
    {
        const unsigned long long count = ++temporary_counter;
        return std::to_wstring(GetCurrentProcessId()) + L"_" +
            std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(count);
    }


    // 想定頻度: ファイル書き込み操作1回につき1回。大容量データでは内部ループ。
    bool write_all(HANDLE file, const std::vector<std::uint8_t>& data)
    {
        std::size_t offset = 0;
        while (offset < data.size())
        {
            const DWORD chunk = static_cast<DWORD>((std::min)(
                data.size() - offset, static_cast<std::size_t>(MAXDWORD)
            ));
            DWORD written = 0;
            if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr) || written == 0) return false;
            offset += written;
        }
        return FlushFileBuffers(file) != FALSE;
    }
}

// 想定頻度: 保存先ディレクトリ取得時。1回の取得で最大2回。
bool storage_system::ensure_directory(const std::wstring& path)
{
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// 想定頻度: 起動時・各保存先アクセス時に数回。
std::wstring storage_system::get_directory_path(directory value, bool create)
{
    const DWORD required = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (required == 0) return {};
    std::wstring app_data(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"APPDATA", app_data.data(), required);
    if (copied == 0 || copied >= required) return {};
    app_data.resize(copied);

    const std::wstring root = app_data + L"\\VoiceLink";
    std::wstring path = root;
    switch (value)
    {
    case directory::logs: path += L"\\logs"; break;
    case directory::cache: path += L"\\cache"; break;
    default: break;
    }
    if (create)
    {
        if (!ensure_directory(root)) return {};
        if (path != root && !ensure_directory(path)) return {};
    }
    else
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return {};
    }
    return path;
}

// 想定頻度: 起動時と設定保存時に各1回程度。
std::wstring storage_system::get_config_path(bool create)
{
    const std::wstring root = get_directory_path(directory::root, create);
    return root.empty() ? std::wstring{} : root + L"\\config.json";
}

// 想定頻度: トレーニング画面表示時・認識結果保存時。
// 想定頻度: 設定ファイルなどのバイナリ読み込み時。
bool storage_system::read_binary(const std::wstring& path, std::vector<std::uint8_t>& data)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool success = GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0 &&
        static_cast<unsigned long long>(size.QuadPart) <= (std::numeric_limits<std::size_t>::max)();
    if (success)
    {
        data.resize(static_cast<std::size_t>(size.QuadPart));
        std::size_t offset = 0;
        while (offset < data.size())
        {
            const DWORD chunk = static_cast<DWORD>((std::min)(data.size() - offset, static_cast<std::size_t>(MAXDWORD)));
            DWORD read = 0;
            if (!ReadFile(file, data.data() + offset, chunk, &read, nullptr) || read == 0) { success = false; break; }
            offset += read;
        }
    }
    CloseHandle(file);
    return success;
}

// 想定頻度: 排他的な新規バイナリファイル作成時。
bool storage_system::write_binary_exclusive(const std::wstring& path, const std::vector<std::uint8_t>& data)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool success = write_all(file, data);
    CloseHandle(file);
    if (!success) DeleteFileW(path.c_str());
    return success;
}

// 想定頻度: 呼び出し側が新規ファイル作成を要求した場合のみ。
bool storage_system::write_binary_new(const std::wstring& path, const std::vector<std::uint8_t>& data)
{
    return write_binary_exclusive(path, data);
}

// 想定頻度: 既存ファイルの安全な更新時。
bool storage_system::write_binary_atomic(const std::wstring& path, const std::vector<std::uint8_t>& data)
{
    const std::wstring temporary = path + L"." + make_unique_suffix() + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool written = write_all(file, data);
    CloseHandle(file);
    const bool moved = written && MoveFileExW(
        temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    ) != FALSE;
    if (!moved) DeleteFileW(temporary.c_str());
    return moved;
}

// 想定頻度: UTF-8 テキストを新規作成するとき。
// 想定頻度: 計測レポートなど、新規テキストファイルの保存時のみ。
bool storage_system::write_utf8_text_new(const std::wstring& path, const std::wstring& text)
{
    std::vector<std::uint8_t> data;
    if (!encode_utf8(text, data, true)) return false;
    return write_binary_exclusive(path, data);
}

// 想定頻度: 汎用UTF-8テキスト保存時のみ。現行サンプル保存経路では未使用。
bool storage_system::write_utf8_text_atomic(const std::wstring& path, const std::wstring& text)
{
    std::vector<std::uint8_t> data;
    if (!encode_utf8(text, data, true)) return false;
    const std::wstring temporary = path + L"." + make_unique_suffix() + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool written = write_all(file, data);
    CloseHandle(file);
    const bool moved = written && MoveFileExW(
        temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    ) != FALSE;
    if (!moved) DeleteFileW(temporary.c_str());
    return moved;
}

// 想定頻度: 認識結果の確定ごとに最大1回。
// 想定頻度: サンプル一覧読み込み・一括適応時にサンプル1件あたり1回。
// 想定頻度: テキスト編集時に1回。旧形式の自動移行時にも最大1回。
// 想定頻度: ユーザーが録音サンプルを保存する操作ごとに1回。
