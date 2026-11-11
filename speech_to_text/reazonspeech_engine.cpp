#include "reazonspeech_engine.h"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#if defined(_WIN64)
#define SHERPA_ONNX_BUILD_MAIN_LIB
#include "../third_party/sherpa-onnx/sherpa-onnx-v2.0.1-win-x64-shared-MD-Release-no-tts/include/sherpa-onnx/c-api/c-api.h"
#endif

#ifndef VOICE_LINK_SOLUTION_DIR
#define VOICE_LINK_SOLUTION_DIR L"."
#endif

namespace
{
#if defined(_WIN64)
    template<typename function_type>
    bool load_function(
        HMODULE module,
        const char* name,
        function_type& function
    )
    {
        function = reinterpret_cast<function_type>(GetProcAddress(module, name));
        return function != nullptr;
    }

    bool to_utf8(const std::wstring& source, std::string& destination)
    {
        if (source.empty())
        {
            destination.clear();
            return true;
        }

        const int required = WideCharToMultiByte(
            CP_UTF8, 0, source.c_str(), -1, nullptr, 0, nullptr, nullptr
        );
        if (required <= 0)
        {
            return false;
        }

        std::string converted(static_cast<size_t>(required), '\0');
        if (WideCharToMultiByte(
            CP_UTF8, 0, source.c_str(), -1,
            converted.data(), required, nullptr, nullptr
        ) <= 0)
        {
            return false;
        }

        converted.resize(static_cast<size_t>(required - 1));
        destination = std::move(converted);
        return true;
    }

    bool from_utf8(const char* source, std::wstring& destination)
    {
        if (!source || !*source)
        {
            destination.clear();
            return true;
        }

        const int required = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, nullptr, 0
        );
        if (required <= 0)
        {
            return false;
        }

        std::wstring converted(static_cast<size_t>(required), L'\0');
        if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, source, -1,
            converted.data(), required
        ) <= 0)
        {
            return false;
        }

        converted.resize(static_cast<size_t>(required - 1));
        destination = std::move(converted);
        return true;
    }
#endif
}

struct reazonspeech_engine::implementation
{
#if defined(_WIN64)
    using create_recognizer_function =
        decltype(&SherpaOnnxCreateOfflineRecognizer);
    using destroy_recognizer_function =
        decltype(&SherpaOnnxDestroyOfflineRecognizer);
    using create_stream_function =
        decltype(&SherpaOnnxCreateOfflineStream);
    using destroy_stream_function =
        decltype(&SherpaOnnxDestroyOfflineStream);
    using accept_waveform_function =
        decltype(&SherpaOnnxAcceptWaveformOffline);
    using decode_stream_function =
        decltype(&SherpaOnnxDecodeOfflineStream);
    using get_result_function =
        decltype(&SherpaOnnxGetOfflineStreamResult);
    using destroy_result_function =
        decltype(&SherpaOnnxDestroyOfflineRecognizerResult);

    HMODULE module = nullptr;
    const SherpaOnnxOfflineRecognizer* recognizer = nullptr;

    create_recognizer_function create_recognizer = nullptr;
    destroy_recognizer_function destroy_recognizer = nullptr;
    create_stream_function create_stream = nullptr;
    destroy_stream_function destroy_stream = nullptr;
    accept_waveform_function accept_waveform = nullptr;
    decode_stream_function decode_stream = nullptr;
    get_result_function get_result = nullptr;
    destroy_result_function destroy_result = nullptr;

    ~implementation()
    {
        if (recognizer && destroy_recognizer)
        {
            destroy_recognizer(recognizer);
            recognizer = nullptr;
        }

        if (module)
        {
            FreeLibrary(module);
            module = nullptr;
        }
    }
#endif
};

reazonspeech_engine::reazonspeech_engine() = default;

reazonspeech_engine::~reazonspeech_engine() = default;

bool reazonspeech_engine::initialize(std::wstring& error)
{
#if !defined(_WIN64)
    error = L"ReazonSpeech は Windows x64 ビルドでのみ利用できます。";
    return false;
#else
    if (_implementation && _implementation->recognizer)
    {
        return true;
    }

    const std::filesystem::path solution_dir(VOICE_LINK_SOLUTION_DIR);
    const std::filesystem::path runtime_dir =
        solution_dir /
        L"third_party" /
        L"sherpa-onnx" /
        L"sherpa-onnx-v2.0.1-win-x64-shared-MD-Release-no-tts" /
        L"lib";
    const std::filesystem::path model_dir =
        solution_dir / L"models" / L"reazonspeech-k2-v2";

    const std::filesystem::path library_path =
        runtime_dir / L"sherpa-onnx-c-api.dll";
    const std::filesystem::path encoder_path =
        model_dir / L"encoder.int8.onnx";
    const std::filesystem::path decoder_path =
        model_dir / L"decoder.int8.onnx";
    const std::filesystem::path joiner_path =
        model_dir / L"joiner.int8.onnx";
    const std::filesystem::path tokens_path =
        model_dir / L"tokens.txt";

    const std::filesystem::path required_paths[] = {
        library_path, encoder_path, decoder_path, joiner_path, tokens_path
    };

    for (const std::filesystem::path& path : required_paths)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            error = L"必要なファイルが見つかりません: " + path.wstring() +
                L"\n先に tools\\setup_reazonspeech.ps1 を実行してください。";
            return false;
        }
    }

    std::unique_ptr<implementation> value =
        std::make_unique<implementation>();

    value->module = LoadLibraryExW(
        library_path.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
    );
    if (!value->module)
    {
        error = L"sherpa-onnx の DLL を読み込めませんでした。エラーコード: " +
            std::to_wstring(GetLastError());
        return false;
    }

    if (!load_function(value->module, "SherpaOnnxCreateOfflineRecognizer", value->create_recognizer) ||
        !load_function(value->module, "SherpaOnnxDestroyOfflineRecognizer", value->destroy_recognizer) ||
        !load_function(value->module, "SherpaOnnxCreateOfflineStream", value->create_stream) ||
        !load_function(value->module, "SherpaOnnxDestroyOfflineStream", value->destroy_stream) ||
        !load_function(value->module, "SherpaOnnxAcceptWaveformOffline", value->accept_waveform) ||
        !load_function(value->module, "SherpaOnnxDecodeOfflineStream", value->decode_stream) ||
        !load_function(value->module, "SherpaOnnxGetOfflineStreamResult", value->get_result) ||
        !load_function(value->module, "SherpaOnnxDestroyOfflineRecognizerResult", value->destroy_result))
    {
        error = L"sherpa-onnx の必要な API が見つかりません。";
        return false;
    }

    std::string encoder_utf8;
    std::string decoder_utf8;
    std::string joiner_utf8;
    std::string tokens_utf8;
    if (!to_utf8(encoder_path.wstring(), encoder_utf8) ||
        !to_utf8(decoder_path.wstring(), decoder_utf8) ||
        !to_utf8(joiner_path.wstring(), joiner_utf8) ||
        !to_utf8(tokens_path.wstring(), tokens_utf8))
    {
        error = L"モデルファイルのパスを UTF-8 に変換できませんでした。";
        return false;
    }

    SherpaOnnxOfflineRecognizerConfig config{};
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = encoder_utf8.c_str();
    config.model_config.transducer.decoder = decoder_utf8.c_str();
    config.model_config.transducer.joiner = joiner_utf8.c_str();
    config.model_config.tokens = tokens_utf8.c_str();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 4;
    config.decoding_method = "greedy_search";

    value->recognizer = value->create_recognizer(&config);
    if (!value->recognizer)
    {
        error = L"ReazonSpeech k2-v2 の認識モデルを初期化できませんでした。";
        return false;
    }

    _implementation = std::move(value);
    error.clear();
    return true;
#endif
}

bool reazonspeech_engine::recognize(
    const std::vector<float>& samples,
    int sample_rate,
    std::wstring& text,
    std::wstring& error
) const
{
#if !defined(_WIN64)
    (void)samples;
    (void)sample_rate;
    text.clear();
    error = L"ReazonSpeech は Windows x64 ビルドでのみ利用できます。";
    return false;
#else
    if (!_implementation || !_implementation->recognizer)
    {
        error = L"音声認識エンジンが初期化されていません。";
        return false;
    }

    if (sample_rate != 16000 ||
        samples.size() < static_cast<size_t>(sample_rate / 4) ||
        samples.size() > static_cast<size_t>(sample_rate * 25))
    {
        error = L"音声データの長さまたはサンプルレートが不正です。";
        return false;
    }

    const SherpaOnnxOfflineStream* stream =
        _implementation->create_stream(_implementation->recognizer);
    if (!stream)
    {
        error = L"音声認識用ストリームを作成できませんでした。";
        return false;
    }

    _implementation->accept_waveform(
        stream,
        sample_rate,
        samples.data(),
        static_cast<int32_t>(samples.size())
    );
    _implementation->decode_stream(_implementation->recognizer, stream);

    const SherpaOnnxOfflineRecognizerResult* result =
        _implementation->get_result(stream);
    if (!result)
    {
        _implementation->destroy_stream(stream);
        error = L"音声認識結果を取得できませんでした。";
        return false;
    }

    const bool converted = from_utf8(result->text, text);
    _implementation->destroy_result(result);
    _implementation->destroy_stream(stream);

    if (!converted)
    {
        error = L"音声認識結果を UTF-8 から変換できませんでした。";
        return false;
    }

    error.clear();
    return true;
#endif
}

bool reazonspeech_engine::is_initialized() const
{
#if !defined(_WIN64)
    return false;
#else
    return _implementation && _implementation->recognizer;
#endif
}
