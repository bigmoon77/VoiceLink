#include "speech_recognition_window.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

namespace
{
    constexpr int window_width = 700;
    constexpr int window_height = 480;
    constexpr int margin = 16;
    constexpr int control_height = 30;
    constexpr int label_height = 22;
    constexpr int record_button_width = 130;
    constexpr int apply_button_width = 160;
}

speech_recognition_window::~speech_recognition_window()
{
    close();
}

bool speech_recognition_window::show(
    HINSTANCE instance,
    HWND owner,
    std::function<void(const std::wstring&)> apply_callback
)
{
    _apply_callback = std::move(apply_callback);

    if (_window)
    {
        ShowWindow(_window, SW_SHOWNORMAL);
        SetForegroundWindow(_window);
        return true;
    }

    _instance = instance;
    _owner = owner;
    _closing = false;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = _instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = window_class_name;

    if (!RegisterClassExW(&window_class) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }

    _window = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        window_class_name,
        L"ReazonSpeech 音声認識",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_width,
        window_height,
        _owner,
        nullptr,
        _instance,
        this
    );

    if (!_window)
    {
        return false;
    }

    ShowWindow(_window, SW_SHOWNORMAL);
    UpdateWindow(_window);
    start_initialize();
    return true;
}

void speech_recognition_window::close()
{
    _closing = true;

    std::wstring error;
    _recorder.stop(error);

    if (_worker.joinable())
    {
        _worker.join();
    }

    if (_window)
    {
        DestroyWindow(_window);
        _window = nullptr;
    }
}

LRESULT CALLBACK speech_recognition_window::window_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
)
{
    speech_recognition_window* self =
        reinterpret_cast<speech_recognition_window*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );

    if (message == WM_NCCREATE)
    {
        const CREATESTRUCTW* create_struct =
            reinterpret_cast<const CREATESTRUCTW*>(l_param);
        self = static_cast<speech_recognition_window*>(
            create_struct->lpCreateParams
        );
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self)
        );
        self->_window = window;
    }

    if (message == WM_NCDESTROY)
    {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (self)
        {
            self->_window = nullptr;
        }
        return DefWindowProcW(window, message, w_param, l_param);
    }

    if (self)
    {
        return self->handle_message(message, w_param, l_param);
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT speech_recognition_window::handle_message(
    UINT message,
    WPARAM w_param,
    LPARAM l_param
)
{
    if (_recorder.handle_message(message, w_param, l_param))
    {
        return 0;
    }

    switch (message)
    {
    case WM_CREATE:
        return create_controls() ? 0 : -1;

    case WM_SIZE:
        layout_controls(LOWORD(l_param), HIWORD(l_param));
        return 0;

    case WM_COMMAND:
        switch (LOWORD(w_param))
        {
        case record_button_id:
            if (HIWORD(w_param) == BN_CLICKED)
            {
                if (_recorder.is_recording())
                {
                    finish_recording();
                }
                else
                {
                    start_recording();
                }
            }
            return 0;

        case apply_button_id:
            if (HIWORD(w_param) == BN_CLICKED)
            {
                apply_result();
            }
            return 0;
        }
        break;

    case audio_recorder::limit_reached_message:
        finish_recording();
        return 0;

    case initialize_completed_message:
        if (_worker.joinable())
        {
            _worker.join();
        }
        {
            std::lock_guard<std::mutex> lock(_worker_mutex);
            _engine_ready = _worker_success;
            if (_engine_ready)
            {
                set_status(L"ReazonSpeech k2-v2 の準備が完了しました。最大25秒録音できます。");
                EnableWindow(_record_button, TRUE);
            }
            else
            {
                set_status(_worker_error);
            }
        }
        return 0;

    case recognition_completed_message:
        if (_worker.joinable())
        {
            _worker.join();
        }
        {
            std::lock_guard<std::mutex> lock(_worker_mutex);
            if (_worker_success)
            {
                SetWindowTextW(_result_edit, _worker_text.c_str());
                EnableWindow(_apply_button, !_worker_text.empty());
                set_status(L"音声認識が完了しました。");
            }
            else
            {
                set_status(_worker_error);
            }
        }
        EnableWindow(_record_button, _engine_ready);
        SetWindowTextW(_record_button, L"録音開始");
        return 0;

    case WM_CLOSE:
        close();
        return 0;

    case WM_DESTROY:
        _closing = true;
        {
            std::wstring error;
            _recorder.stop(error);
        }
        _window = nullptr;
        return 0;

    }

    return DefWindowProcW(_window, message, w_param, l_param);
}

bool speech_recognition_window::create_controls()
{
    _status_text = CreateWindowExW(
        0,
        L"STATIC",
        L"モデルを読み込んでいます...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(status_text_id)),
        _instance,
        nullptr
    );

    _record_button = CreateWindowExW(
        0,
        L"BUTTON",
        L"録音開始",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(record_button_id)),
        _instance,
        nullptr
    );

    _apply_button = CreateWindowExW(
        0,
        L"BUTTON",
        L"読み上げ欄へ反映",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(apply_button_id)),
        _instance,
        nullptr
    );

    _result_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT |
            ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(result_edit_id)),
        _instance,
        nullptr
    );

    if (!_status_text || !_record_button || !_apply_button || !_result_edit)
    {
        return false;
    }

    EnableWindow(_record_button, FALSE);
    EnableWindow(_apply_button, FALSE);

    apply_control_font(_status_text);
    apply_control_font(_record_button);
    apply_control_font(_apply_button);
    apply_control_font(_result_edit);

    RECT client{};
    GetClientRect(_window, &client);
    layout_controls(client.right, client.bottom);
    return true;
}

void speech_recognition_window::layout_controls(int width, int height)
{
    if (!_status_text || !_record_button || !_apply_button || !_result_edit)
    {
        return;
    }

    const int content_width = (std::max)(width - margin * 2, 100);
    const int buttons_y = height - margin - control_height;
    const int result_y = margin + label_height + margin;
    const int result_height = (std::max)(
        buttons_y - result_y - margin - control_height - margin,
        80
    );

    MoveWindow(
        _status_text,
        margin,
        margin,
        content_width,
        label_height,
        TRUE
    );
    MoveWindow(
        _result_edit,
        margin,
        result_y,
        content_width,
        result_height,
        TRUE
    );
    MoveWindow(
        _record_button,
        margin,
        buttons_y,
        record_button_width,
        control_height,
        TRUE
    );
    MoveWindow(
        _apply_button,
        width - margin - apply_button_width,
        buttons_y,
        apply_button_width,
        control_height,
        TRUE
    );
}

void speech_recognition_window::apply_control_font(HWND control)
{
    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
        TRUE
    );
}

void speech_recognition_window::set_status(const std::wstring& message)
{
    if (_status_text)
    {
        SetWindowTextW(_status_text, message.c_str());
    }
}

void speech_recognition_window::start_initialize()
{
    _worker = std::thread([this]()
    {
        std::wstring error;
        bool success = false;
        try
        {
            success = _engine.initialize(error);
        }
        catch (const std::exception&)
        {
            error = L"音声認識エンジンの初期化中に例外が発生しました。";
        }
        catch (...)
        {
            error = L"音声認識エンジンの初期化に失敗しました。";
        }

        {
            std::lock_guard<std::mutex> lock(_worker_mutex);
            _worker_success = success;
            _worker_error = std::move(error);
            _worker_text.clear();
        }

        if (!_closing && _window)
        {
            PostMessageW(_window, initialize_completed_message, 0, 0);
        }
    });
}

void speech_recognition_window::start_recording()
{
    std::wstring error;
    if (!_recorder.start(_window, error))
    {
        set_status(error);
        return;
    }

    SetWindowTextW(_record_button, L"録音停止");
    EnableWindow(_apply_button, FALSE);
    set_status(L"録音中です。もう一度押すと認識を開始します（最大25秒）。");
}

void speech_recognition_window::finish_recording()
{
    if (!_recorder.is_recording() && _recorder.get_samples().empty())
    {
        return;
    }

    std::wstring error;
    if (!_recorder.stop(error))
    {
        set_status(error);
        SetWindowTextW(_record_button, L"録音開始");
        EnableWindow(_record_button, _engine_ready);
        return;
    }

    SetWindowTextW(_record_button, L"録音開始");
    const std::vector<float> samples = _recorder.get_samples();
    if (samples.size() < static_cast<size_t>(audio_recorder::sample_rate / 4))
    {
        set_status(L"録音時間が短すぎます。もう一度録音してください。");
        return;
    }

    start_recognition(samples);
}

void speech_recognition_window::start_recognition(const std::vector<float>& samples)
{
    EnableWindow(_record_button, FALSE);
    EnableWindow(_apply_button, FALSE);
    set_status(L"音声を認識しています...");

    _worker = std::thread([this, samples]()
    {
        std::wstring text;
        std::wstring error;
        bool success = false;
        try
        {
            success = _engine.recognize(
                samples,
                audio_recorder::sample_rate,
                text,
                error
            );
        }
        catch (...)
        {
            error = L"音声認識中に例外が発生しました。";
        }

        {
            std::lock_guard<std::mutex> lock(_worker_mutex);
            _worker_success = success;
            _worker_error = std::move(error);
            _worker_text = std::move(text);
        }

        if (!_closing && _window)
        {
            PostMessageW(_window, recognition_completed_message, 0, 0);
        }
    });
}

void speech_recognition_window::apply_result()
{
    if (!_result_edit || !_apply_callback)
    {
        return;
    }

    const int length = GetWindowTextLengthW(_result_edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(
        _result_edit,
        text.data(),
        length + 1
    );
    text.resize(static_cast<size_t>((std::max)(copied, 0)));
    if (!text.empty())
    {
        _apply_callback(text);
    }
}
