#pragma once

#include "speech_recognition_engine.h"

#include <Windows.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class main_window;

class stt_controller
{
public:
    stt_controller(main_window& window, std::unique_ptr<speech_recognition_engine> engine);
    ~stt_controller();

    stt_controller(const stt_controller&) = delete;
    stt_controller& operator=(const stt_controller&) = delete;

private:
    static constexpr UINT recognition_message = WM_APP + 10;

    enum class message_type
    {
        recognized_text,
        status
    };

    struct queued_message
    {
        message_type type = message_type::status;
        std::wstring text;
    };

    void start_recognition();
    void stop_recognition();
    void enqueue_message(message_type type, const std::wstring& text);
    bool handle_window_message(UINT message, WPARAM w_param, LPARAM l_param);
    void update_buttons();

    main_window& _window;
    std::unique_ptr<speech_recognition_engine> _engine;
    std::thread _worker;
    std::atomic_bool _stop_requested{ false };
    std::atomic_bool _running{ false };
    std::mutex _message_mutex;
    std::deque<queued_message> _messages;
};
