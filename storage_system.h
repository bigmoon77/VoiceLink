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

    // 想定頻度: 起動時・保存先アクセス時に数回。    static std::wstring get_directory_path(directory value, bool create);
    // 想定頻度: 起動・設定保存時に各1回程度。    static std::wstring get_config_path(bool create);
    // 想定頻度: トレーニング画面の表示・履歴保存時に各1回程度。    static std::wstring get_training_history_path(bool create);

    // 想定頻度: サンプル読み込み・再生・適応時。選択サンプル数に比例。    static bool read_binary(const std::wstring& path, std::vector<std::uint8_t>& data);
    // 想定頻度: 必要時のみ。通常のサンプル保存では内部処理から呼び出す。    static bool write_binary_new(const std::wstring& path, const std::vector<std::uint8_t>& data);
    // 想定頻度: WAV内テキスト更新時にサンプル1件あたり1回。    static bool write_binary_atomic(const std::wstring& path, const std::vector<std::uint8_t>& data);
    // 想定頻度: 起動時の履歴読み込みと旧形式サンプル移行時のみ。    static bool read_utf8_text(const std::wstring& path, std::wstring& text);
    // 想定頻度: 現行の録音サンプル保存経路では未使用。互換・汎用用途で必要時。    static bool write_utf8_text_atomic(const std::wstring& path, const std::wstring& text);
    // 想定頻度: 認識結果の確定ごとに最大1回。    static bool append_utf8_text(const std::wstring& path, const std::wstring& text, bool bom);
    enum class training_text_state { found, missing, invalid };
    // 想定頻度: サンプル一覧の読み込み時と一括適応時にサンプル1件あたり1回。    static training_text_state read_training_sample_text(const std::wstring& wav_path, std::wstring& text);
    // 想定頻度: テキスト編集時に1回。旧形式サンプルの初回読み込み時にも最大1回。    static bool write_training_sample_text_atomic(const std::wstring& wav_path, const std::wstring& text);
    // 想定頻度: ユーザーが録音サンプルを保存する操作ごとに1回。
    static bool save_training_sample(
        const std::vector<std::uint8_t>& wav_data,
        const std::wstring& text,
        std::wstring& wav_path
    );

private:
    // 想定頻度: 保存先ディレクトリ取得時。1回の取得で最大2回。    static bool ensure_directory(const std::wstring& path);
    // 想定頻度: 排他的な新規ファイル保存時。上位の保存処理1回につき1回。    static bool write_binary_exclusive(const std::wstring& path, const std::vector<std::uint8_t>& data);
};
