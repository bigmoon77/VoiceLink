#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

class storage_system
{
public:
    enum class directory
    {
        root,
        training_samples,
        training_templates,
        logs,
        cache
    };

    static std::wstring get_directory_path(directory value, bool create);
    static std::wstring get_config_path(bool create);
    static std::wstring get_training_history_path(bool create);

    static bool read_binary(const std::wstring& path, std::vector<std::uint8_t>& data);
    static bool write_binary_new(const std::wstring& path, const std::vector<std::uint8_t>& data);
    static bool write_binary_atomic(const std::wstring& path, const std::vector<std::uint8_t>& data);
    static bool read_utf8_text(const std::wstring& path, std::wstring& text);
    static bool write_utf8_text_atomic(const std::wstring& path, const std::wstring& text);
    static bool append_utf8_text(const std::wstring& path, const std::wstring& text, bool bom);
    static bool save_training_sample(
        const std::vector<std::uint8_t>& wav_data,
        const std::wstring& text,
        std::wstring& wav_path
    );

private:
    static bool ensure_directory(const std::wstring& path);
    static bool write_binary_exclusive(const std::wstring& path, const std::vector<std::uint8_t>& data);
};
