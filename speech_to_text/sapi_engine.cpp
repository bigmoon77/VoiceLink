#include "sapi_engine.h"

#include <Windows.h>
#include <sapi.h>
#include <wrl/client.h>

#include <string>

#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    struct com_apartment_guard
    {
        bool initialized = false;

        ~com_apartment_guard()
        {
            if (initialized)
            {
                CoUninitialize();
            }
        }
    };

    std::wstring get_hresult_message(HRESULT result)
    {
        wchar_t* message_buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(result),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&message_buffer),
            0,
            nullptr
        );

        std::wstring message;
        if (length > 0 && message_buffer)
        {
            message.assign(message_buffer, length);
            LocalFree(message_buffer);
        }
        else
        {
            message = L"HRESULT: 0x";
            wchar_t code[16]{};
            swprintf_s(code, L"%08X", static_cast<unsigned int>(result));
            message += code;
        }
        return message;
    }

    bool set_error(HRESULT result, const wchar_t* operation, std::wstring& error)
    {
        if (FAILED(result))
        {
            error = operation;
            error += L": ";
            error += get_hresult_message(result);
            return false;
        }
        return true;
    }
}

bool sapi_engine::run(
    const std::atomic_bool& stop_requested,
    const std::function<void(const std::wstring&)>& on_recognized,
    std::wstring& error
)
{
    const HRESULT apartment_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment_result))
    {
        error = L"SAPI の COM 初期化に失敗しました: ";
        error += get_hresult_message(apartment_result);
        return false;
    }

    com_apartment_guard apartment_guard;
    apartment_guard.initialized = true;

    bool success = false;
    Microsoft::WRL::ComPtr<ISpRecognizer> recognizer;
    Microsoft::WRL::ComPtr<ISpRecoContext> context;
    Microsoft::WRL::ComPtr<ISpRecoGrammar> grammar;

    HRESULT result = CoCreateInstance(
        CLSID_SpSharedRecognizer,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(recognizer.GetAddressOf())
    );

    if (!set_error(result, L"SAPI 音声認識エンジンを作成できません", error))
    {
        return false;
    }

    result = recognizer->SetInput(nullptr, TRUE);
    if (!set_error(result, L"既定のマイク入力を設定できません", error))
    {
        CoUninitialize();
        return false;
    }

    result = recognizer->CreateRecoContext(context.GetAddressOf());
    if (!set_error(result, L"音声認識コンテキストを作成できません", error))
    {
        CoUninitialize();
        return false;
    }

    result = context->SetNotifyWin32Event();
    if (!set_error(result, L"音声認識イベントを初期化できません", error))
    {
        CoUninitialize();
        return false;
    }

    result = context->SetInterest(SPFEI(SPEI_RECOGNITION), SPFEI(SPEI_RECOGNITION));
    if (!set_error(result, L"音声認識イベントを登録できません", error))
    {
        CoUninitialize();
        return false;
    }

    result = context->CreateGrammar(1, grammar.GetAddressOf());
    if (!set_error(result, L"音声認識文法を作成できません", error))
    {
        CoUninitialize();
        return false;
    }

    result = grammar->DictationLoad(nullptr, SPLO_STATIC);
    if (!set_error(result, L"ディクテーション文法を読み込めません。Windows に音声認識言語がインストールされているか確認してください", error))
    {
        CoUninitialize();
        return false;
    }

    result = grammar->DictationSetState(SPRS_ACTIVE);
    if (!set_error(result, L"音声認識を開始できません", error))
    {
        CoUninitialize();
        return false;
    }

    success = true;
    const HANDLE event_handle = context->GetNotifyEventHandle();
    if (!event_handle || event_handle == INVALID_HANDLE_VALUE)
    {
        error = L"音声認識イベントハンドルを取得できません。";
        success = false;
    }

    while (success && (!stop_requested.load())
    {
        const DWORD wait_result = WaitForSingleObject(event_handle, 100);
        if (wait_result == WAIT_TIMEOUT)
        {
            continue;
        }
        if (wait_result != WAIT_OBJECT_0)
        {
            error = L"音声認識イベントの待機に失敗しました。";
            success = false;
            break;
        }

        for (;;)
        {
            SPEVENT speech_event{};
            ULONG event_count = 0;
            result = context->GetEvents(1, &speech_event, &event_count);
            if (FAILED(result) || event_count == 0)
            {
                break;
            }

            if (speech_event.eEventId == SPEI_RECOGNITION &&
                speech_event.elParamType == SPET_LPARAM_IS_OBJECT &&
                speech_event.lParam != 0)
            {
                IUnknown* event_object = reinterpret_cast<IUnknown*>(speech_event.lParam);
                Microsoft::WRL::ComPtr<ISpRecoResult> recognition_result;
                const HRESULT query_result = event_object->QueryInterface(
                    IID_PPV_ARGS(recognition_result.GetAddressOf())
                );
                event_object->Release();

                if (SUCCEEDED(query_result) && recognition_result)
                {
                    WCHAR* recognized_text = nullptr;
                    const HRESULT text_result = recognition_result->GetText(
                        SP_GETWHOLEPHRASE,
                        SP_GETWHOLEPHRASE,
                        TRUE,
                        &recognized_text,
                        nullptr
                    );

                    if (SUCCEEDED(text_result) && recognized_text)
                    {
                        if (on_recognized)
                        {
                            on_recognized(recognized_text);
                        }
                        CoTaskMemFree(recognized_text);
                    }
                }
            }
            else if (speech_event.elParamType == SPET_LPARAM_IS_OBJECT && speech_event.lParam != 0)
            {
                reinterpret_cast<IUnknown*>(speech_event.lParam)->Release();
            }
        }
    }

    if (grammar)
    {
        grammar->DictationSetState(SPRS_INACTIVE);
    }

    grammar.Reset();
    context.Reset();
    recognizer.Reset();
    return success;
}
