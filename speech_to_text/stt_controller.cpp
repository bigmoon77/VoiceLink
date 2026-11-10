#include "stt_controller.h"

#include "../main_window.h"

#include <utility>

stt_controller::stt_controller(main_window& window, std::unique_ptr<speech_recognition_engine> engine)
    : _window(window)
    , _engine(std::move(engine))
{
    _window.set_recognition_start_callback([this]()
    {
        start_recognition();
    });
    _window.set_recognition_stop_callback([this]()
    {
        stop_recognition();
    });
    _window.set_speech_message_callback([this](UINT message, WPARAM w_param, LPARAM l_param)
    {
        return handle_window_message(message, w_param, l_param);
    });
    update_buttons();
}

stt_controller::~stt_controller()
{
    _window.set_recognition_start_callback({});
    _window.set_recognition_stop_callback({});
    _window.set_speech_message_callback({});
    _stop_requested = true;

    if (_worker.joinable())
    {
        _worker.join();
    }

    std::lock_guard<std::mutex> lock(_message_mutex);
    _messages.clear();
}

void stt_controller::start_recognition()
{
    if (_running.exchange(true))
    {
        return;
    }

    if (_worker.joinable())
    {
        _worker.join();
    }

    _stop_requested = false;
    update_buttons();
    _window.set_status(L"マイク入力を開始しています...");

    _worker = std::thread([this]()
    {
        std::wstring error;
        bool success = false;
        try
        {
            success = _engine->run(
                _stop_requested,
                [this](const std::wstring& text)
                {
                    enqueue_message(message_type::recognized_text, text);
                },
                error
            );
        }
        catch (...)
        {
            error = L"音声認識中に予期しないエラーが発生しました。";
        }

        _running = false;
        if (!error.empty())
        {
            enqueue_message(message_type::status, L"音声認識エラー: " + error);
        }
        else if (success)
        {
            enqueue_message(message_type::status, L"音声認識を停止しました。");
        }
        else
        {
            enqueue_message(message_type::status, L"音声認識を終了しました。");
        }
    });
}

void stt_controller::stop_recognition()
{
    if (!_running.load())
    {
        return;
    }

    _stop_requested = true;
    _window.set_status(L"音声認識を停止しています...");
}

void stt_controller::enqueue_message(message_type type, const std::wstring& text)
{
    {
        std::lock_guard<std::mutex> lock(_message_mutex);
        _messages.push_back({ type, text });
    }

    PostMessageW(_window.get_handle(), recognition_message, 0, 0);
}

bool stt_controller::handle_window_message(UINT message, WPARAM, LPARAM)
{
    if (message != recognition_message)
    {
        return false;
    }

    std::deque<queued_message> messages;
    {
        std::lock_guard<std::mutex> lock(_message_mutex);
        messages.swap(_messages);
    }

    for (const queued_message& queued : messages)
    {
        if (queued.type == message_type::recognized_text)
        {
            _window.append_recognized_text(queued.text);
        }
        else
        {
            _window.set_status(queued.text);
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
