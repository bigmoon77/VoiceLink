#include "storage_system.h"

#include <algorithm>
#include <atomic>
#include <limits>

namespace
{
    std::atomic<unsigned long long> temporary_counter{ 0 };

    bool has_utf8_bom(const std::vector<std::uint8_t>& data)
    {
        return data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF;
    }

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

    std::wstring make_unique_suffix()
    {
        const unsigned long long count = ++temporary_counter;
        return std::to_wstring(GetCurrentProcessId()) + L"_" +
            std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(count);
    }

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

bool storage_system::ensure_directory(const std::wstring& path)
{
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

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
    case directory::training_samples: path += L"\\training_samples"; break;
    case directory::training_templates: path += L"\\training_templates"; break;
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

std::wstring storage_system::get_config_path(bool create)
{
    const std::wstring root = get_directory_path(directory::root, create);
    return root.empty() ? std::wstring{} : root + L"\\config.json";
}

std::wstring storage_system::get_training_history_path(bool create)
{
    const std::wstring root = get_directory_path(directory::root, create);
    return root.empty() ? std::wstring{} : root + L"\\sapi_training.txt";
}

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

bool storage_system::write_binary_exclusive(const std::wstring& path, const std::vector<std::uint8_t>& data)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool success = write_all(file, data);
    CloseHandle(file);
    if (!success) DeleteFileW(path.c_str());
    return success;
}

bool storage_system::write_binary_new(const std::wstring& path, const std::vector<std::uint8_t>& data)
{
    return write_binary_exclusive(path, data);
}

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

bool storage_system::read_utf8_text(const std::wstring& path, std::wstring& text)
{
    std::vector<std::uint8_t> data;
    if (!read_binary(path, data)) return false;
    if (has_utf8_bom(data)) data.erase(data.begin(), data.begin() + 3);
    if (data.empty()) { text.clear(); return true; }
    if (data.size() > static_cast<std::size_t>(INT_MAX)) return false;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), nullptr, 0);
    if (count <= 0) return false;
    text.resize(static_cast<std::size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), text.data(), count) == count;
}

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

bool storage_system::append_utf8_text(const std::wstring& path, const std::wstring& text, bool bom)
{
    std::vector<std::uint8_t> data;
    if (!encode_utf8(text, data, false)) return false;
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool success = GetFileSizeEx(file, &size) != FALSE;
    if (success && size.QuadPart == 0 && bom)
    {
        const std::vector<std::uint8_t> mark{ 0xEF, 0xBB, 0xBF };
        success = write_all(file, mark);
    }
    if (success) success = write_all(file, data);
    CloseHandle(file);
    return success;
}

bool storage_system::save_training_sample(
    const std::vector<std::uint8_t>& wav_data,
    const std::wstring& text,
    std::wstring& wav_path
)
{
    const std::wstring directory_path = get_directory_path(directory::training_samples, true);
    if (directory_path.empty()) return false;

    std::vector<std::uint8_t> text_data;
    if (!encode_utf8(text, text_data, true)) return false;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t timestamp[64]{};
    swprintf_s(timestamp, L"sample_%04u%02u%02u_%02u%02u%02u_%03u",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    const std::wstring base = timestamp;

    for (unsigned int suffix = 0; suffix < 100000; ++suffix)
    {
        const std::wstring name = suffix == 0 ? base : base + L"_" + std::to_wstring(suffix);
        const std::wstring stem = directory_path + L"\\" + name;
        const std::wstring wav_final = stem + L".wav";
        const std::wstring text_final = stem + L".txt";
        const std::wstring lock_path = stem + L".lock";
        if (GetFileAttributesW(wav_final.c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(text_final.c_str()) != INVALID_FILE_ATTRIBUTES) continue;

        HANDLE lock = CreateFileW(lock_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (lock == INVALID_HANDLE_VALUE) continue;

        const std::wstring suffix_id = L"." + make_unique_suffix() + L".tmp";
        const std::wstring wav_temp = stem + suffix_id + L".wav";
        const std::wstring text_temp = stem + suffix_id + L".txt";
        bool success = write_binary_exclusive(wav_temp, wav_data) &&
            write_binary_exclusive(text_temp, text_data);
        DWORD move_error = ERROR_SUCCESS;

        if (success)
        {
            success = MoveFileExW(wav_temp.c_str(), wav_final.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
            if (!success) move_error = GetLastError();
        }
        if (success)
        {
            success = MoveFileExW(text_temp.c_str(), text_final.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
            if (!success)
            {
                move_error = GetLastError();
                DeleteFileW(wav_final.c_str());
            }
        }

        if (!success)
        {
            DeleteFileW(wav_temp.c_str());
            DeleteFileW(text_temp.c_str());
        }
        CloseHandle(lock);
        DeleteFileW(lock_path.c_str());

        if (success)
        {
            wav_path = wav_final;
            return true;
        }
        if (move_error != ERROR_ALREADY_EXISTS && move_error != ERROR_FILE_EXISTS) return false;
    }
    return false;
}
