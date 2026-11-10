#include "sapi_engine.h"

#include <cwchar>
#include <string>
#include <utility>

#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    struct com_apartment_guard
    {
        bool initialized = false;
        ~com_apartment_guard() { if (initialized) CoUninitialize(); }
    };

    std::wstring get_hresult_message(HRESULT result)
    {
        wchar_t* message_buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(result), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&message_buffer), 0, nullptr);
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

sapi_engine::sapi_engine()
{
    _command_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

sapi_engine::~sapi_engine()
{
    if (_command_event)
    {
        CloseHandle(_command_event);
        _command_event = nullptr;
    }
}

bool sapi_engine::supports_training() const
{
    return _command_event != nullptr;
}

bool sapi_engine::select_alternate(std::uint64_t result_id, std::size_t alternate_index)
{
    if (!_running.load() || !_command_event) return false;
    command value;
    value.type = command_type::commit_alternate;
    value.result_id = result_id;
    value.alternate_index = alternate_index;
    enqueue_command(value);
    return true;
}

bool sapi_engine::submit_adaptation_data(const std::wstring& text)
{
    return submit_adaptation_batch(std::vector<std::wstring>{ text }, 1);
}

bool sapi_engine::submit_adaptation_batch(const std::vector<std::wstring>& texts, std::size_t repetitions)
{
    if (!_running.load() || !_command_event || texts.empty() || repetitions == 0 || repetitions > 1000) return false;
    if (texts.size() > 100000 / repetitions) return false;
    for (const std::wstring& text : texts)
    {
        if (text.empty() || text.size() > 512) return false;
    }

    bool expected = false;
    if (!_adaptation_pending.compare_exchange_strong(expected, true)) return false;

    command value;
    value.type = command_type::adaptation_data;
    value.texts = texts;
    value.repetitions = repetitions;
    enqueue_command(value);
    return true;
}

void sapi_engine::enqueue_command(const command& value)
{
    {
        std::lock_guard<std::mutex> lock(_command_mutex);
        _commands.push_back(value);
        SetEvent(_command_event);
    }
}

bool sapi_engine::run(
    const std::atomic_bool& stop_requested,
    const std::function<void(const speech_recognition_result&)>& on_recognized,
    const std::function<void(const std::wstring&)>& on_status,
    std::wstring& error)
{
    if (!_command_event)
    {
        error = L"SAPI トレーニング用イベントを作成できません。";
        return false;
    }

    const HRESULT apartment_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment_result))
    {
        error = L"SAPI の COM 初期化に失敗しました: ";
        error += get_hresult_message(apartment_result);
        return false;
    }
    com_apartment_guard apartment_guard;
    apartment_guard.initialized = true;

    Microsoft::WRL::ComPtr<ISpRecognizer> recognizer;
    Microsoft::WRL::ComPtr<ISpRecoContext> context;
    Microsoft::WRL::ComPtr<ISpRecoGrammar> grammar;

    HRESULT result = CoCreateInstance(CLSID_SpSharedRecognizer, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(recognizer.GetAddressOf()));
    if (!set_error(result, L"SAPI 音声認識エンジンを作成できません", error)) return false;

    result = recognizer->SetInput(nullptr, TRUE);
    if (!set_error(result, L"既定のマイク入力を設定できません", error)) return false;

    result = recognizer->CreateRecoContext(context.GetAddressOf());
    if (!set_error(result, L"音声認識コンテキストを作成できません", error)) return false;

    result = context->SetNotifyWin32Event();
    if (!set_error(result, L"音声認識イベントを初期化できません", error)) return false;

    const ULONGLONG event_interest = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_ADAPTATION);
    result = context->SetInterest(event_interest, event_interest);
    if (!set_error(result, L"音声認識イベントを登録できません", error)) return false;

    result = context->CreateGrammar(1, grammar.GetAddressOf());
    if (!set_error(result, L"音声認識文法を作成できません", error)) return false;

    result = grammar->LoadDictation(nullptr, SPLO_STATIC);
    if (!set_error(result, L"ディクテーション文法を読み込めません。Windows に音声認識言語がインストールされているか確認してください", error)) return false;

    result = grammar->SetDictationState(SPRS_ACTIVE);
    if (!set_error(result, L"音声認識を開始できません", error)) return false;

    const HANDLE notify_event = context->GetNotifyEventHandle();
    if (!notify_event || notify_event == INVALID_HANDLE_VALUE)
    {
        error = L"音声認識イベントハンドルを取得できません。";
        grammar->SetDictationState(SPRS_INACTIVE);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(_command_mutex);
        _commands.clear();
        ResetEvent(_command_event);
    }
    _pending_alternates.clear();
    _adaptation_pending = false;
    ULONGLONG adaptation_start_tick = 0;
    std::deque<std::wstring> adaptation_queue;
    _running = true;

    const auto send_next_adaptation = [&]()
    {
        if (adaptation_queue.empty())
        {
            _adaptation_pending = false;
            adaptation_start_tick = 0;
            if (on_status) on_status(L"サンプル文章の適応処理がすべて完了しました。");
            return;
        }

        const std::wstring text = std::move(adaptation_queue.front());
        adaptation_queue.pop_front();
        const HRESULT adaptation_result = context->SetAdaptationData(text.c_str(), static_cast<ULONG>(text.size()));
        if (FAILED(adaptation_result))
        {
            adaptation_queue.clear();
            _adaptation_pending = false;
            adaptation_start_tick = 0;
            if (on_status) on_status(L"学習データの送信に失敗しました: " + get_hresult_message(adaptation_result));
            return;
        }

        adaptation_start_tick = GetTickCount64();
        if (on_status)
        {
            on_status(L"SAPIへ文章を送信しました。残り " + std::to_wstring(adaptation_queue.size()) + L" 件の適応処理を待っています。");
        }
    };

    const auto process_commands = [&]()
    {
        std::deque<command> commands;
        {
            std::lock_guard<std::mutex> lock(_command_mutex);
            commands.swap(_commands);
            ResetEvent(_command_event);
        }

        for (const command& value : commands)
        {
            if (value.type == command_type::commit_alternate)
            {
                const auto found = _pending_alternates.find(value.result_id);
                if (found == _pending_alternates.end() || value.alternate_index >= found->second.size())
                {
                    if (on_status) on_status(L"代替候補の有効期限が切れています。もう一度認識してください。");
                    continue;
                }
                const HRESULT commit_result = found->second[value.alternate_index]->Commit();
                if (on_status)
                {
                    on_status(SUCCEEDED(commit_result)
                        ? L"選択した認識候補をSAPIへ確定しました。"
                        : L"認識候補の確定に失敗しました: " + get_hresult_message(commit_result));
                }
                _pending_alternates.erase(found);
            }
            else
            {
                adaptation_queue.clear();
                for (std::size_t repetition = 0; repetition < value.repetitions; ++repetition)
                {
                    for (const std::wstring& text : value.texts)
                    {
                        adaptation_queue.push_back(text);
                    }
                }
                send_next_adaptation();
            }
        }
    };

    bool success = true;
    HANDLE wait_handles[] = { notify_event, _command_event };
    while (!stop_requested.load())
    {
        const DWORD wait_result = WaitForMultipleObjects(2, wait_handles, FALSE, 100);
        if (wait_result == WAIT_TIMEOUT)
        {
            if (_adaptation_pending.load() && adaptation_start_tick != 0 && GetTickCount64() - adaptation_start_tick > 30000)
            {
                adaptation_queue.clear();
                _adaptation_pending = false;
                adaptation_start_tick = 0;
                if (on_status) on_status(L"SAPIから適応完了通知がありません。残りの一括処理を中止しました。");
            }
            continue;
        }
        if (wait_result == WAIT_OBJECT_0 + 1)
        {
            process_commands();
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
            if (FAILED(result))
            {
                error = L"音声認識イベントを取得できません: ";
                error += get_hresult_message(result);
                success = false;
                break;
            }
            if (event_count == 0) break;

            if (speech_event.eEventId == SPEI_RECOGNITION &&
                speech_event.elParamType == SPET_LPARAM_IS_OBJECT && speech_event.lParam != 0)
            {
                IUnknown* event_object = reinterpret_cast<IUnknown*>(speech_event.lParam);
                Microsoft::WRL::ComPtr<ISpRecoResult> recognition_result;
                const HRESULT query_result = event_object->QueryInterface(IID_PPV_ARGS(recognition_result.GetAddressOf()));
                event_object->Release();

                if (SUCCEEDED(query_result) && recognition_result)
                {
                    WCHAR* recognized_text = nullptr;
                    const HRESULT text_result = recognition_result->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &recognized_text, nullptr);
                    if (SUCCEEDED(text_result) && recognized_text)
                    {
                        speech_recognition_result record;
                        record.id = _next_result_id++;
                        record.text = recognized_text;
                        CoTaskMemFree(recognized_text);

                        SPPHRASE* phrase = nullptr;
                        if (SUCCEEDED(recognition_result->GetPhrase(&phrase)) && phrase)
                        {
                            constexpr ULONG max_alternates = 5;
                            ISpPhraseAlt* raw_alternates[max_alternates]{};
                            ULONG alternate_count = 0;
                            const HRESULT alternate_result = recognition_result->GetAlternates(
                                phrase->Rule.ulFirstElement,
                                phrase->Rule.ulCountOfElements,
                                max_alternates,
                                raw_alternates,
                                &alternate_count);
                            std::vector<Microsoft::WRL::ComPtr<ISpPhraseAlt>> stored_alternates;
                            if (SUCCEEDED(alternate_result))
                            {
                                for (ULONG i = 0; i < alternate_count; ++i)
                                {
                                    Microsoft::WRL::ComPtr<ISpPhraseAlt> alternate;
                                    alternate.Attach(raw_alternates[i]);
                                    if (!alternate) continue;
                                    WCHAR* alternate_text = nullptr;
                                    if (SUCCEEDED(alternate->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &alternate_text, nullptr)) && alternate_text)
                                    {
                                        record.alternatives.emplace_back(alternate_text);
                                        CoTaskMemFree(alternate_text);
                                        stored_alternates.push_back(std::move(alternate));
                                    }
                                }
                            }
                            else
                            {
                                for (ISpPhraseAlt* alternate : raw_alternates) if (alternate) alternate->Release();
                            }
                            CoTaskMemFree(phrase);
                            if (!stored_alternates.empty())
                            {
                                _pending_alternates[record.id] = std::move(stored_alternates);
                                while (_pending_alternates.size() > 50) _pending_alternates.erase(_pending_alternates.begin());
                            }
                        }

                        if (on_recognized) on_recognized(record);
                    }
                }
            }
            else if (speech_event.eEventId == SPEI_ADAPTATION)
            {
                adaptation_start_tick = 0;
                send_next_adaptation();
            }
            else if (speech_event.elParamType == SPET_LPARAM_IS_OBJECT && speech_event.lParam != 0)
            {
                reinterpret_cast<IUnknown*>(speech_event.lParam)->Release();
            }
        }
        if (!success) break;
    }

    process_commands();
    _running = false;
    _adaptation_pending = false;
    _pending_alternates.clear();
    {
        std::lock_guard<std::mutex> lock(_command_mutex);
        _commands.clear();
        ResetEvent(_command_event);
    }
    grammar->SetDictationState(SPRS_INACTIVE);
    return success;
}
