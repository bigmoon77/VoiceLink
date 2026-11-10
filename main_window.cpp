#include "main_window.h"

#include <CommCtrl.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")

constexpr wchar_t main_window::_window_class_name[];

namespace
{
    constexpr int window_width = 600;
    constexpr int window_height = 540;
    constexpr int margin = 16;
    constexpr int control_height = 26;
    constexpr int label_height = 20;
    constexpr int play_button_width = 100;
    constexpr int reconnect_button_width = 100;
    constexpr int volume_test_button_width = 110;
}

bool main_window::create(HINSTANCE instance, int show_command)
{
    _instance = instance;

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&common_controls))
    {
        return false;
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = _instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = _window_class_name;

    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }

    _window = CreateWindowExW(
        0,
        _window_class_name,
        L"VoiceLink",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_width,
        window_height,
        nullptr,
        nullptr,
        _instance,
        this
    );

    if (!_window)
    {
        return false;
    }

    ShowWindow(_window, show_command);
    UpdateWindow(_window);
    return true;
}

void main_window::set_speakers(const std::vector<speaker_item>& speakers)
{
    if (!_speaker_combo)
    {
        return;
    }

    SendMessageW(_speaker_combo, CB_RESETCONTENT, 0, 0);

    for (const auto& speaker : speakers)
    {
        const LRESULT index = SendMessageW(
            _speaker_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(speaker.name.c_str())
        );

        if (index != CB_ERR && index != CB_ERRSPACE)
        {
            SendMessageW(
                _speaker_combo,
                CB_SETITEMDATA,
                static_cast<WPARAM>(index),
                static_cast<LPARAM>(speaker.speaker_id)
            );
        }
    }

    if (!speakers.empty())
    {
        SendMessageW(_speaker_combo, CB_SETCURSEL, 0, 0);
    }
}

void main_window::set_status(const std::wstring& message)
{
    if (_status_text)
    {
        SetWindowTextW(_status_text, message.c_str());
    }
}

void main_window::set_play_enabled(bool enabled)
{
    if (_play_button)
    {
        EnableWindow(_play_button, enabled);
    }
}

void main_window::set_reconnect_enabled(bool enabled)
{
    if (_reconnect_button)
    {
        EnableWindow(_reconnect_button, enabled);
    }
}

void main_window::set_volume(int volume)
{
    volume = std::clamp(volume, 0, 100);

    if (_volume_slider)
    {
        SendMessageW(_volume_slider, TBM_SETPOS, TRUE, volume);
    }

    update_volume_label(volume);
}

std::wstring main_window::get_text() const
{
    if (!_text_edit)
    {
        return {};
    }

    const int length = GetWindowTextLengthW(_text_edit);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(_text_edit, buffer.data(), length + 1);
    return std::wstring(buffer.data(), static_cast<size_t>(std::max(copied, 0)));
}

bool main_window::get_selected_speaker_id(int& speaker_id) const
{
    if (!_speaker_combo)
    {
        return false;
    }

    const LRESULT selected_index = SendMessageW(_speaker_combo, CB_GETCURSEL, 0, 0);
    if (selected_index == CB_ERR)
    {
        return false;
    }

    const LRESULT selected_id = SendMessageW(
        _speaker_combo,
        CB_GETITEMDATA,
        static_cast<WPARAM>(selected_index),
        0
    );

    if (selected_id == CB_ERR)
    {
        return false;
    }

    speaker_id = static_cast<int>(selected_id);
    return true;
}

void main_window::set_play_callback(std::function<void()> callback)
{
    _play_callback = std::move(callback);
}

void main_window::set_reconnect_callback(std::function<void()> callback)
{
    _reconnect_callback = std::move(callback);
}

void main_window::set_volume_callback(std::function<void(int)> callback)
{
    _volume_callback = std::move(callback);
}

void main_window::set_volume_test_callback(std::function<void()> callback)
{
    _volume_test_callback = std::move(callback);
}

void main_window::set_message_callback(std::function<bool(UINT, WPARAM, LPARAM)> callback)
{
    _message_callback = std::move(callback);
}

HWND main_window::get_handle() const
{
    return _window;
}

LRESULT CALLBACK main_window::window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    main_window* self = reinterpret_cast<main_window*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        self = static_cast<main_window*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->_window = window;
    }

    if (self)
    {
        return self->handle_message(message, w_param, l_param);
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT main_window::handle_message(UINT message, WPARAM w_param, LPARAM l_param)
{
    if (_message_callback && _message_callback(message, w_param, l_param))
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

    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(l_param) == _volume_slider)
        {
            const int volume = static_cast<int>(SendMessageW(_volume_slider, TBM_GETPOS, 0, 0));
            update_volume_label(volume);
            if (_volume_callback)
            {
                _volume_callback(volume);
            }
            return 0;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(w_param))
        {
        case play_button_id:
            if (HIWORD(w_param) == BN_CLICKED && _play_callback)
            {
                _play_callback();
            }
            return 0;

        case reconnect_button_id:
            if (HIWORD(w_param) == BN_CLICKED && _reconnect_callback)
            {
                _reconnect_callback();
            }
            return 0;

        case volume_test_button_id:
            if (HIWORD(w_param) == BN_CLICKED && _volume_test_callback)
            {
                _volume_test_callback();
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(_window, message, w_param, l_param);
}

bool main_window::create_controls()
{
    _speaker_label = CreateWindowExW(
        0, L"STATIC", L"Speaker / Style",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(speaker_label_id)),
        _instance, nullptr
    );

    _speaker_combo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(speaker_combo_id)),
        _instance, nullptr
    );

    _reconnect_button = CreateWindowExW(
        0, L"BUTTON", L"Reconnect",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(reconnect_button_id)),
        _instance, nullptr
    );

    _text_label = CreateWindowExW(
        0, L"STATIC", L"Text",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(text_label_id)),
        _instance, nullptr
    );

    _text_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(text_edit_id)),
        _instance, nullptr
    );

    _volume_label = CreateWindowExW(
        0, L"STATIC", L"Volume: 100%",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(volume_label_id)),
        _instance, nullptr
    );

    _volume_slider = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(volume_slider_id)),
        _instance, nullptr
    );

    _volume_test_button = CreateWindowExW(
        0, L"BUTTON", L"Test Volume",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(volume_test_button_id)),
        _instance, nullptr
    );

    _play_button = CreateWindowExW(
        0, L"BUTTON", L"Play",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(play_button_id)),
        _instance, nullptr
    );

    _status_text = CreateWindowExW(
        0, L"STATIC", L"VOICEVOX Engine の接続待ち",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(status_text_id)),
        _instance, nullptr
    );

    const bool controls_created =
        _speaker_label && _speaker_combo && _reconnect_button &&
        _text_label && _text_edit && _volume_label && _volume_slider &&
        _volume_test_button && _play_button && _status_text;

    if (!controls_created)
    {
        return false;
    }

    SendMessageW(_volume_slider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(_volume_slider, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(_volume_slider, TBM_SETPOS, TRUE, 100);

    apply_control_font(_speaker_label);
    apply_control_font(_speaker_combo);
    apply_control_font(_reconnect_button);
    apply_control_font(_text_label);
    apply_control_font(_text_edit);
    apply_control_font(_volume_label);
    apply_control_font(_volume_test_button);
    apply_control_font(_play_button);
    apply_control_font(_status_text);

    return true;
}

void main_window::layout_controls(int width, int height)
{
    if (!_speaker_label || !_speaker_combo || !_reconnect_button ||
        !_text_label || !_text_edit || !_volume_label || !_volume_slider ||
        !_volume_test_button || !_play_button || !_status_text)
    {
        return;
    }

    const int content_width = std::max(width - margin * 2, 100);
    const int status_y = height - margin - label_height;
    const int buttons_y = status_y - margin - control_height;
    const int volume_y = buttons_y - margin - control_height;
    const int volume_label_y = volume_y - label_height;
    const int edit_y = margin + label_height + control_height + margin + label_height;
    const int edit_height = std::max(volume_label_y - margin - edit_y - margin, 60);
    const int speaker_combo_width = std::max(content_width - reconnect_button_width - margin, 80);

    MoveWindow(_speaker_label, margin, margin, content_width, label_height, TRUE);
    MoveWindow(_speaker_combo, margin, margin + label_height, speaker_combo_width, 240, TRUE);
    MoveWindow(
        _reconnect_button,
        margin + speaker_combo_width + margin,
        margin + label_height,
        reconnect_button_width,
        control_height,
        TRUE
    );

    MoveWindow(_text_label, margin, edit_y - label_height, content_width, label_height, TRUE);
    MoveWindow(_text_edit, margin, edit_y, content_width, edit_height, TRUE);

    MoveWindow(_volume_label, margin, volume_label_y, content_width, label_height, TRUE);
    MoveWindow(
        _volume_slider,
        margin,
        volume_y,
        std::max(content_width - volume_test_button_width - margin, 80),
        control_height,
        TRUE
    );
    MoveWindow(
        _volume_test_button,
        width - margin - volume_test_button_width,
        volume_y,
        volume_test_button_width,
        control_height,
        TRUE
    );

    MoveWindow(
        _play_button,
        width - margin - play_button_width,
        buttons_y,
        play_button_width,
        control_height,
        TRUE
    );
    MoveWindow(_status_text, margin, status_y, content_width - play_button_width - margin, label_height, TRUE);
}

void main_window::apply_control_font(HWND control)
{
    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
        TRUE
    );
}

void main_window::update_volume_label(int volume)
{
    if (!_volume_label)
    {
        return;
    }

    const std::wstring label = L"Volume: " + std::to_wstring(volume) + L"%";
    SetWindowTextW(_volume_label, label.c_str());
}
