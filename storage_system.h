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
        logs,
        cache
    };

    // 想定頻度: 起動時・保存先アクセス時に数回。
    static std::wstring get_directory_path(directory value, bool create);
    // 想定頻度: 起動・設定保存時に各1回程度。
    static std::wstring get_config_path(bool create);
    // 想定頻度: 設定ファイルなどのバイナリ読み込み時。
    static bool read_binary(const std::wstring& path, std::vector<std::uint8_t>& data);
    // 想定頻度: 排他的な新規バイナリファイル作成時。
    static bool write_binary_new(const std::wstring& path, const std::vector<std::uint8_t>& data);
    // 想定頻度: 既存ファイルの安全な更新時。
    static bool write_binary_atomic(const std::wstring& path, const std::vector<std::uint8_t>& data);
    // 想定頻度: UTF-8 テキストを新規作成するとき。
    static bool write_utf8_text_new(const std::wstring& path, const std::wstring& text);
    // 想定頻度: UTF-8 テキストを安全に更新するとき。
    static bool write_utf8_text_atomic(const std::wstring& path, const std::wstring& text);
private:
    // 想定頻度: 保存先ディレクトリ取得時。1回の取得で最大2回。
    static bool ensure_directory(const std::wstring& path);
    // 想定頻度: 排他的な新規ファイル保存時。上位の保存処理1回につき1回。
    static bool write_binary_exclusive(const std::wstring& path, const std::vector<std::uint8_t>& data);
};
