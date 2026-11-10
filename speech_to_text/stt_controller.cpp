#include "stt_controller.h"

#include "main_window.h"

#include <utility>

stt_controller::stt_controller(main_window& window, std::unique_ptr<speech_recognition_engine> engine)
    : _window(window)
    , _engine(std::move(engine))
    , _training_window(window.get_handle())
{
    _window.set_recognition_start_callback([this]() { start_recognition(); });
    _window.set_recognition_stop_callback([this]() { stop_recognition(); });
    _window.set_training_enabled(_engine && _engine->supports_training());
    _window.set_training_callback([this]() { _training_window.show(); });
    _window.set_speech_message_callback([this](UINT message, WPARAM w_param, LPARAM l_param)
    {
        return handle_window_message(message, w_param, l_param);
    });
    _training_window.set_alternate_callback([this](std::uint64_t result_id, std::size_t alternate_index)
    {
        commit_alternate(result_id, alternate_index);
    });
    _training_window.set_adaptation_callback([this](const std::wstring& text)
    {
        submit_adaptation(text);
    });
    update_buttons();
}

stt_controller::~stt_controller()
{
    _window.set_recognition_start_callback({});
    _window.set_recognition_stop_callback({});
    _window.set_training_callback({});
    _window.set_training_enabled(false);
    _window.set_speech_message_callback({});
    _stop_requested = true;
    if (_worker.joinable()) _worker.join();
    std::lock_guard<std::mutex> lock(_message_mutex);
    _messages.clear();
}

void stt_controller::start_recognition()
{
    if (!_engine || _running.exchange(true)) return;
    if (_worker.joinable()) _worker.join();

    _stop_requested = false;
    update_buttons();
    _window.set_status(L"マイク入力を開始しています...");
    _training_window.set_status(L"音声認識を開始しています...");

    _worker = std::thread([this]()
    {
        std::wstring error;
        bool success = false;
        try
        {
            success = _engine->run(
                _stop_requested,
                [this](const speech_recognition_result& result) { enqueue_recognition(result); },
                [this](const std::wstring& status) { enqueue_status(status); },
                error);
        }
        catch (...)
        {
            error = L"音声認識中に予期しないエラーが発生しました。";
        }

        _running = false;
        if (!error.empty()) enqueue_status(L"音声認識エラー: " + error);
        else if (success) enqueue_status(L"音声認識を停止しました。");
        else enqueue_status(L"音声認識を終了しました。");
    });
}

void stt_controller::stop_recognition()
{
    if (!_running.load()) return;
    _stop_requested = true;
    _window.set_status(L"音声認識を停止しています...");
}

void stt_controller::enqueue_recognition(const speech_recognition_result& result)
{
    {
        std::lock_guard<std::mutex> lock(_message_mutex);
        queued_message message;
        message.type = message_type::recognized_result;
        message.recognition = result;
        _messages.push_back(std::move(message));
    }
    PostMessageW(_window.get_handle(), recognition_message, 0, 0);
}

void stt_controller::enqueue_status(const std::wstring& text)
{
    {
        std::lock_guard<std::mutex> lock(_message_mutex);
        queued_message message;
        message.type = message_type::status;
        message.text = text;
        _messages.push_back(std::move(message));
    }
    PostMessageW(_window.get_handle(), recognition_message, 0, 0);
}

bool stt_controller::handle_window_message(UINT message, WPARAM, LPARAM)
{
    if (message != recognition_message) return false;
    std::deque<queued_message> messages;
    {
        std::lock_guard<std::mutex> lock(_message_mutex);
        messages.swap(_messages);
    }

    for (const queued_message& queued : messages)
    {
        if (queued.type == message_type::recognized_result)
        {
            _window.append_recognized_text(queued.recognition.text);
            _training_window.add_record(queued.recognition);
        }
        else
        {
            _window.set_status(queued.text);
            _training_window.set_status(queued.text);
        }
    }
    update_buttons();
    return true;
}

void stt_controller::update_buttons()
{
    const bool running = _running.load();
    _window.set_recognition_start_enabled(!running);
    _window.set_recognition_stop_enabled(running);
}

void stt_controller::commit_alternate(std::uint64_t result_id, std::size_t alternate_index)
{
    if (!_engine || !_engine->select_alternate(result_id, alternate_index))
    {
        _training_window.set_status(L"候補を確定できません。音声認識中に操作してください。");
        return;
    }
    _training_window.set_status(L"候補の確定要求をSAPIへ送信しました。");
}

void stt_controller::submit_adaptation(const std::wstring& text)
{
    if (!_engine || !_engine->submit_adaptation_data(text))
    {
        _training_window.set_status(L"学習データを送信できません。認識中か、前回の適応処理が完了しているか確認してください。文字数上限は512文字です。");
        return;
    }
    _training_window.set_status(L"学習データの送信を要求しました。");
}
