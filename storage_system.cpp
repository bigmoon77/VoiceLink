#include "storage_system.h"

#include <algorithm>
#include <climits>
#include <cwchar>
#include <atomic>
#include <limits>
#include <cstring>

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


    constexpr std::uint32_t training_text_chunk_version = 1;

    std::uint32_t read_u32_le(const std::vector<std::uint8_t>& data, std::size_t offset)
    {
        return static_cast<std::uint32_t>(data[offset]) |
            (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    }

    void append_u32_le(std::vector<std::uint8_t>& data, std::uint32_t value)
    {
        data.push_back(static_cast<std::uint8_t>(value));
        data.push_back(static_cast<std::uint8_t>(value >> 8));
        data.push_back(static_cast<std::uint8_t>(value >> 16));
        data.push_back(static_cast<std::uint8_t>(value >> 24));
    }

    bool decode_utf8(const std::uint8_t* data, std::size_t size, std::wstring& text)
    {
        if (size == 0)
        {
            text.clear();
            return true;
        }
        if (size > static_cast<std::size_t>(INT_MAX)) return false;

        const int count = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(data),
            static_cast<int>(size), nullptr, 0
        );
        if (count <= 0) return false;

        text.resize(static_cast<std::size_t>(count));
        return MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(data),
            static_cast<int>(size), text.data(), count
        ) == count;
    }

    storage_system::training_text_state extract_training_text(
        const std::vector<std::uint8_t>& wav,
        std::wstring& text
    )
    {
        using state = storage_system::training_text_state;
        if (wav.size() < 12 ||
            std::memcmp(wav.data(), "RIFF", 4) != 0 ||
            std::memcmp(wav.data() + 8, "WAVE", 4) != 0)
        {
            return state::invalid;
        }

        const std::uint64_t riff_end_64 = 8ull + read_u32_le(wav, 4);
        if (riff_end_64 < 12 || riff_end_64 > wav.size()) return state::invalid;

        const std::size_t riff_end = static_cast<std::size_t>(riff_end_64);
        std::size_t offset = 12;
        bool found = false;
        while (offset < riff_end)
        {
            if (riff_end - offset < 8) return state::invalid;

            const std::uint32_t chunk_size = read_u32_le(wav, offset + 4);
            const std::size_t payload_offset = offset + 8;
            if (chunk_size > riff_end - payload_offset) return state::invalid;

            const std::size_t next_offset =
                payload_offset + static_cast<std::size_t>(chunk_size) + (chunk_size & 1u);
            if (next_offset > riff_end) return state::invalid;

            if (std::memcmp(wav.data() + offset, "vtxt", 4) == 0)
            {
                if (found || chunk_size < 4 ||
                    read_u32_le(wav, payload_offset) != training_text_chunk_version ||
                    !decode_utf8(wav.data() + payload_offset + 4, chunk_size - 4, text))
                {
                    return state::invalid;
                }
                found = true;
            }
            offset = next_offset;
        }

        return found ? state::found : state::missing;
    }

    bool embed_training_text(
        const std::vector<std::uint8_t>& source,
        const std::wstring& text,
        std::vector<std::uint8_t>& output
    )
    {
        if (source.size() < 12 ||
            std::memcmp(source.data(), "RIFF", 4) != 0 ||
            std::memcmp(source.data() + 8, "WAVE", 4) != 0)
        {
            return false;
        }

        const std::uint64_t riff_end_64 = 8ull + read_u32_le(source, 4);
        if (riff_end_64 < 12 || riff_end_64 > source.size()) return false;
        const std::size_t riff_end = static_cast<std::size_t>(riff_end_64);

        std::vector<std::uint8_t> encoded_text;
        if (!encode_utf8(text, encoded_text, false) ||
            encoded_text.size() > static_cast<std::size_t>(MAXDWORD) - 4)
        {
            return false;
        }

        output.assign(source.begin(), source.begin() + 12);
        std::size_t offset = 12;
        while (offset < riff_end)
        {
            if (riff_end - offset < 8) return false;

            const std::uint32_t chunk_size = read_u32_le(source, offset + 4);
            const std::size_t payload_offset = offset + 8;
            if (chunk_size > riff_end - payload_offset) return false;

            const std::size_t next_offset =
                payload_offset + static_cast<std::size_t>(chunk_size) + (chunk_size & 1u);
            if (next_offset > riff_end) return false;

            if (std::memcmp(source.data() + offset, "vtxt", 4) != 0)
            {
                output.insert(output.end(), source.begin() + offset, source.begin() + next_offset);
            }
            offset = next_offset;
        }

        output.insert(output.end(), { 'v', 't', 'x', 't' });
        append_u32_le(output, static_cast<std::uint32_t>(encoded_text.size() + 4));
        append_u32_le(output, training_text_chunk_version);
        output.insert(output.end(), encoded_text.begin(), encoded_text.end());
        if ((encoded_text.size() + 4) & 1u) output.push_back(0);

        output.insert(output.end(), source.begin() + riff_end, source.end());
        const std::size_t riff_size = output.size() - 8 - (source.size() - riff_end);
        if (riff_size > MAXDWORD) return false;

        const std::uint32_t riff_size_32 = static_cast<std::uint32_t>(riff_size);
        output[4] = static_cast<std::uint8_t>(riff_size_32);
        output[5] = static_cast<std::uint8_t>(riff_size_32 >> 8);
        output[6] = static_cast<std::uint8_t>(riff_size_32 >> 16);
        output[7] = static_cast<std::uint8_t>(riff_size_32 >> 24);
        return true;
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

    const std::wstring lock_path = path + L".lock";
    HANDLE lock = INVALID_HANDLE_VALUE;
    for (unsigned int attempt = 0; attempt < 1000; ++attempt)
    {
        lock = CreateFileW(lock_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (lock != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return false;
        Sleep(10);
    }
    if (lock == INVALID_HANDLE_VALUE) return false;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE | FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool success = file != INVALID_HANDLE_VALUE;
    if (success)
    {
        LARGE_INTEGER size{};
        success = GetFileSizeEx(file, &size) != FALSE;
        if (success && size.QuadPart == 0 && bom)
        {
            const std::vector<std::uint8_t> mark{ 0xEF, 0xBB, 0xBF };
            success = write_all(file, mark);
        }
        if (success) success = write_all(file, data);
        CloseHandle(file);
    }

    CloseHandle(lock);
    DeleteFileW(lock_path.c_str());
    return success;
}


storage_system::training_text_state storage_system::read_training_sample_text(
    const std::wstring& wav_path,
    std::wstring& text
)
{
    std::vector<std::uint8_t> wav_data;
    if (!read_binary(wav_path, wav_data)) return training_text_state::invalid;
    return extract_training_text(wav_data, text);
}

bool storage_system::write_training_sample_text_atomic(
    const std::wstring& wav_path,
    const std::wstring& text
)
{
    std::vector<std::uint8_t> wav_data;
    std::vector<std::uint8_t> updated_data;
    if (!read_binary(wav_path, wav_data) ||
        !embed_training_text(wav_data, text, updated_data))
    {
        return false;
    }
    return write_binary_atomic(wav_path, updated_data);
}

bool storage_system::save_training_sample(
    const std::vector<std::uint8_t>& wav_data,
    const std::wstring& text,
    std::wstring& wav_path
)
{
    const std::wstring directory_path = get_directory_path(directory::training_samples, true);
    if (directory_path.empty()) return false;

    std::vector<std::uint8_t> embedded_wav;
    if (!embed_training_text(wav_data, text, embedded_wav)) return false;

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
        const std::wstring legacy_text = stem + L".txt";
        const std::wstring lock_path = stem + L".lock";
        if (GetFileAttributesW(wav_final.c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(legacy_text.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            continue;
        }

        HANDLE lock = CreateFileW(
            lock_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr
        );
        if (lock == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
            return false;
        }

        const std::wstring temporary = stem + L"." + make_unique_suffix() + L".tmp";
        bool success = write_binary_exclusive(temporary, embedded_wav);
        DWORD operation_error = success ? ERROR_SUCCESS : GetLastError();
        if (success)
        {
            success = MoveFileExW(temporary.c_str(), wav_final.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
            if (!success) operation_error = GetLastError();
        }
        if (!success) DeleteFileW(temporary.c_str());

        CloseHandle(lock);
        DeleteFileW(lock_path.c_str());

        if (success)
        {
            wav_path = wav_final;
            return true;
        }
        if (operation_error != ERROR_ALREADY_EXISTS && operation_error != ERROR_FILE_EXISTS) return false;
    }
    return false;
}
