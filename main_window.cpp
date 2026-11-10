#include "main_window.h"

#include <algorithm>
#include <utility>
#include <vector>

constexpr wchar_t main_window::_window_class_name[];

namespace
{
    constexpr int window_width = 560;
    constexpr int window_height = 420;
    constexpr int margin = 16;
    constexpr int control_height = 26;
    constexpr int label_height = 20;
    constexpr int play_button_width = 100;
}

bool main_window::create(HINSTANCE instance, int show_command)
{
    _instance = instance;

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

    case WM_COMMAND:
        if (LOWORD(w_param) == play_button_id && HIWORD(w_param) == BN_CLICKED)
        {
            if (_play_callback)
            {
                _play_callback();
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
        0,
        L"STATIC",
        L"Speaker / Style",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(speaker_label_id)),
        _instance,
        nullptr
    );

    _speaker_combo = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(speaker_combo_id)),
        _instance,
        nullptr
    );

    _text_label = CreateWindowExW(
        0,
        L"STATIC",
        L"Debug Text",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(text_label_id)),
        _instance,
        nullptr
    );

    _text_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(text_edit_id)),
        _instance,
        nullptr
    );

    _play_button = CreateWindowExW(
        0,
        L"BUTTON",
        L"Play",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(play_button_id)),
        _instance,
        nullptr
    );

    _status_text = CreateWindowExW(
        0,
        L"STATIC",
        L"VOICEVOX Engine の接続待ち",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0,
        _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(status_text_id)),
        _instance,
        nullptr
    );

    const bool controls_created =
        _speaker_label && _speaker_combo && _text_label &&
        _text_edit && _play_button && _status_text;

    if (!controls_created)
    {
        return false;
    }

    apply_control_font(_speaker_label);
    apply_control_font(_speaker_combo);
    apply_control_font(_text_label);
    apply_control_font(_text_edit);
    apply_control_font(_play_button);
    apply_control_font(_status_text);

    return true;
}

void main_window::layout_controls(int width, int height)
{
    if (!_speaker_label || !_speaker_combo || !_text_label ||
        !_text_edit || !_play_button || !_status_text)
    {
        return;
    }

    const int content_width = std::max(width - margin * 2, 100);
    const int status_y = height - margin - label_height;
    const int button_y = status_y - margin - control_height;
    const int edit_y = margin + label_height + control_height + margin + label_height;
    const int edit_height = std::max(button_y - margin - edit_y, 60);

    MoveWindow(_speaker_combo, margin, margin + label_height, content_width, 240, TRUE);
    MoveWindow(
        _text_edit,
        margin,
        edit_y,
        content_width,
        edit_height,
        TRUE
    );
    MoveWindow(
        _play_button,
        width - margin - play_button_width,
        button_y,
        play_button_width,
        control_height,
        TRUE
    );
    MoveWindow(_status_text, margin, status_y, content_width - play_button_width - margin, label_height, TRUE);

    MoveWindow(_speaker_label, margin, margin, content_width, label_height, TRUE);
    MoveWindow(_text_label, margin, edit_y - label_height, content_width, label_height, TRUE);
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
