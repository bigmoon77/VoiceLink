#include "main_window.h"

#include <CommCtrl.h>
#include <ShObjIdl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

constexpr wchar_t main_window::_window_class_name[];

namespace
{
    constexpr int window_width = 760;
    constexpr int window_height = 860;
    constexpr int margin = 16;
    constexpr int control_height = 26;
    constexpr int label_height = 20;
    constexpr int play_button_width = 100;
    constexpr int reconnect_button_width = 100;
    constexpr int volume_test_button_width = 110;
    constexpr int path_button_width = 80;
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

void main_window::set_selected_engine(int engine_index)
{
    if (_engine_select_combo)
    {
        SendMessageW(_engine_select_combo, CB_SETCURSEL, engine_index == 1 ? 1 : 0, 0);
        update_engine_controls();
    }
}

int main_window::get_selected_engine() const
{
    if (!_engine_select_combo)
    {
        return 0;
    }

    const LRESULT selected = SendMessageW(_engine_select_combo, CB_GETCURSEL, 0, 0);
    return selected == 1 ? 1 : 0;
}

void main_window::set_gpt_sovits_settings(const gpt_sovits_form_settings& value)
{
    if (_gpt_host_edit)
    {
        SetWindowTextW(_gpt_host_edit, value.host.c_str());
    }
    if (_gpt_port_edit)
    {
        SetWindowTextW(_gpt_port_edit, std::to_wstring(value.port).c_str());
    }
    if (_gpt_reference_edit)
    {
        SetWindowTextW(_gpt_reference_edit, value.reference_audio_path.c_str());
    }
    if (_gpt_prompt_edit)
    {
        SetWindowTextW(_gpt_prompt_edit, value.prompt_text.c_str());
    }

    const auto select_language = [](HWND combo, const std::wstring& language)
    {
        if (!combo)
        {
            return;
        }

        const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
        for (LRESULT index = 0; index < count; ++index)
        {
            wchar_t item[32]{};
            SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(item));
            if (language == item)
            {
                SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
                return;
            }
        }
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    };

    select_language(_gpt_prompt_language_combo, value.prompt_language);
    select_language(_gpt_text_language_combo, value.text_language);
}

main_window::gpt_sovits_form_settings main_window::get_gpt_sovits_settings() const
{
    gpt_sovits_form_settings value;

    const auto read_text = [](HWND control) -> std::wstring
    {
        if (!control)
        {
            return {};
        }
        const int length = GetWindowTextLengthW(control);
        std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(control, buffer.data(), length + 1);
        return std::wstring(buffer.data(), static_cast<size_t>(std::max(copied, 0)));
    };

    value.host = read_text(_gpt_host_edit);
    const std::wstring port_text = read_text(_gpt_port_edit);
    wchar_t* end = nullptr;
    const unsigned long port = std::wcstoul(port_text.c_str(), &end, 10);
    if (port_text.empty() || !end || *end != L'\0' || port == 0 || port > 65535)
    {
        value.port = 0;
    }
    else
    {
        value.port = static_cast<std::uint16_t>(port);
    }

    value.reference_audio_path = read_text(_gpt_reference_edit);
    value.prompt_text = read_text(_gpt_prompt_edit);

    const auto read_language = [](HWND combo) -> std::wstring
    {
        if (!combo)
        {
            return L"ja";
        }
        const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (index == CB_ERR)
        {
            return L"ja";
        }
        wchar_t language[32]{};
        SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(language));
        return language;
    };

    value.prompt_language = read_language(_gpt_prompt_language_combo);
    value.text_language = read_language(_gpt_text_language_combo);
    return value;
}

void main_window::set_voicevox_path(const std::wstring& path)
{
    if (_engine_path_edit)
    {
        SetWindowTextW(_engine_path_edit, path.c_str());
    }
}

std::wstring main_window::get_voicevox_path() const
{
    if (!_engine_path_edit)
    {
        return {};
    }

    const int length = GetWindowTextLengthW(_engine_path_edit);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(_engine_path_edit, buffer.data(), length + 1);
    return std::wstring(buffer.data(), static_cast<size_t>(std::max(copied, 0)));
}

void main_window::set_engine_settings_enabled(bool enabled)
{
    if (_engine_select_combo)
    {
        EnableWindow(_engine_select_combo, enabled);
    }
    if (_gpt_host_edit)
    {
        EnableWindow(_gpt_host_edit, enabled);
    }
    if (_gpt_port_edit)
    {
        EnableWindow(_gpt_port_edit, enabled);
    }
    if (_gpt_reference_edit)
    {
        EnableWindow(_gpt_reference_edit, enabled);
    }
    if (_gpt_reference_browse_button)
    {
        EnableWindow(_gpt_reference_browse_button, enabled);
    }
    if (_gpt_prompt_edit)
    {
        EnableWindow(_gpt_prompt_edit, enabled);
    }
    if (_gpt_prompt_language_combo)
    {
        EnableWindow(_gpt_prompt_language_combo, enabled);
    }
    if (_gpt_text_language_combo)
    {
        EnableWindow(_gpt_text_language_combo, enabled);
    }
    if (_gpt_settings_apply_button)
    {
        EnableWindow(_gpt_settings_apply_button, enabled);
    }
    if (_engine_path_edit)
    {
        EnableWindow(_engine_path_edit, enabled);
    }
    if (_engine_path_browse_button)
    {
        EnableWindow(_engine_path_browse_button, enabled);
    }
    if (_engine_path_apply_button)
    {
        EnableWindow(_engine_path_apply_button, enabled);
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

void main_window::set_text(const std::wstring& text)
{
    if (_text_edit)
    {
        SetWindowTextW(_text_edit, text.c_str());
    }
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

void main_window::set_voicevox_path_callback(std::function<void(const std::wstring&)> callback)
{
    _voicevox_path_callback = std::move(callback);
}

void main_window::set_engine_changed_callback(std::function<void(int)> callback)
{
    _engine_changed_callback = std::move(callback);
}

void main_window::set_gpt_sovits_settings_callback(
    std::function<void(const gpt_sovits_form_settings&)> callback
)
{
    _gpt_sovits_settings_callback = std::move(callback);
}

void main_window::set_speech_recognition_callback(std::function<void()> callback)
{
    _speech_recognition_callback = std::move(callback);
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

        case speech_recognition_button_id:
            if (HIWORD(w_param) == BN_CLICKED && _speech_recognition_callback)
            {
                _speech_recognition_callback();
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

        case engine_path_browse_button_id:
            if (HIWORD(w_param) == BN_CLICKED)
            {
                browse_voicevox_path();
            }
            return 0;

        case engine_select_combo_id:
            if (HIWORD(w_param) == CBN_SELCHANGE)
            {
                const int engine_index = get_selected_engine();
                update_engine_controls();
                if (_engine_changed_callback)
                {
                    _engine_changed_callback(engine_index);
                }
            }
            return 0;

        case gpt_reference_browse_button_id:
            if (HIWORD(w_param) == BN_CLICKED)
            {
                browse_gpt_reference_audio();
            }
            return 0;

        case gpt_settings_apply_button_id:
            if (HIWORD(w_param) == BN_CLICKED && _gpt_sovits_settings_callback)
            {
                const gpt_sovits_form_settings value = get_gpt_sovits_settings();
                if (value.host.empty() || value.port == 0)
                {
                    set_status(L"GPT-SoVITS のホスト名とポート番号を確認してください。");
                    return 0;
                }
                if (value.reference_audio_path.empty() || value.prompt_text.empty())
                {
                    set_status(L"参照音声と参照テキストを設定してください。");
                    return 0;
                }
                const DWORD attributes = GetFileAttributesW(value.reference_audio_path.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES ||
                    (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    set_status(L"参照音声ファイルが見つかりません。");
                    return 0;
                }
                _gpt_sovits_settings_callback(value);
            }
            return 0;

        case engine_path_apply_button_id:
            if (HIWORD(w_param) == BN_CLICKED && _voicevox_path_callback)
            {
                _voicevox_path_callback(get_voicevox_path());
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
    _engine_select_label = CreateWindowExW(
        0, L"STATIC", L"TTS Engine",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(engine_select_label_id)),
        _instance, nullptr
    );

    _engine_select_combo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(engine_select_combo_id)),
        _instance, nullptr
    );
    if (_engine_select_combo)
    {
        SendMessageW(_engine_select_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"VOICEVOX"));
        SendMessageW(_engine_select_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"GPT-SoVITS"));
        SendMessageW(_engine_select_combo, CB_SETCURSEL, 0, 0);
    }

    _gpt_host_label = CreateWindowExW(
        0, L"STATIC", L"API Host",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_host_label_id)),
        _instance, nullptr
    );
    _gpt_host_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_host_edit_id)),
        _instance, nullptr
    );
    _gpt_port_label = CreateWindowExW(
        0, L"STATIC", L"Port",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_port_label_id)),
        _instance, nullptr
    );
    _gpt_port_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"9880",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_NUMBER,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_port_edit_id)),
        _instance, nullptr
    );
    _gpt_reference_label = CreateWindowExW(
        0, L"STATIC", L"Reference Audio",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_reference_label_id)),
        _instance, nullptr
    );
    _gpt_reference_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_reference_edit_id)),
        _instance, nullptr
    );
    _gpt_reference_browse_button = CreateWindowExW(
        0, L"BUTTON", L"Browse...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_reference_browse_button_id)),
        _instance, nullptr
    );
    _gpt_prompt_label = CreateWindowExW(
        0, L"STATIC", L"Reference Text",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_prompt_label_id)),
        _instance, nullptr
    );
    _gpt_prompt_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_prompt_edit_id)),
        _instance, nullptr
    );
    _gpt_prompt_language_label = CreateWindowExW(
        0, L"STATIC", L"Reference Language",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_prompt_language_label_id)),
        _instance, nullptr
    );
    _gpt_prompt_language_combo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_prompt_language_combo_id)),
        _instance, nullptr
    );
    _gpt_text_language_label = CreateWindowExW(
        0, L"STATIC", L"Output Language",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_text_language_label_id)),
        _instance, nullptr
    );
    _gpt_text_language_combo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_text_language_combo_id)),
        _instance, nullptr
    );
    _gpt_settings_apply_button = CreateWindowExW(
        0, L"BUTTON", L"Apply GPT-SoVITS Settings",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(gpt_settings_apply_button_id)),
        _instance, nullptr
    );

    _engine_path_label = CreateWindowExW(
        0, L"STATIC", L"VOICEVOX Engine (run.exe)",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(engine_path_label_id)),
        _instance, nullptr
    );

    _engine_path_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(engine_path_edit_id)),
        _instance, nullptr
    );

    _engine_path_browse_button = CreateWindowExW(
        0, L"BUTTON", L"Browse...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(engine_path_browse_button_id)),
        _instance, nullptr
    );

    _engine_path_apply_button = CreateWindowExW(
        0, L"BUTTON", L"Apply",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(engine_path_apply_button_id)),
        _instance, nullptr
    );

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
    _speech_recognition_button = CreateWindowExW(
        0, L"BUTTON", L"音声認識",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, _window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(speech_recognition_button_id)),
        _instance, nullptr
    );

    const bool controls_created =
        _engine_select_label && _engine_select_combo &&
        _gpt_host_label && _gpt_host_edit && _gpt_port_label && _gpt_port_edit &&
        _gpt_reference_label && _gpt_reference_edit && _gpt_reference_browse_button &&
        _gpt_prompt_label && _gpt_prompt_edit && _gpt_prompt_language_label &&
        _gpt_prompt_language_combo && _gpt_text_language_label && _gpt_text_language_combo &&
        _gpt_settings_apply_button && _engine_path_label && _engine_path_edit && _engine_path_browse_button &&
        _engine_path_apply_button && _speaker_label && _speaker_combo &&
        _reconnect_button && _text_label && _text_edit && _volume_label && _volume_slider &&
        _volume_test_button && _play_button && _status_text && _speech_recognition_button;

    if (!controls_created)
    {
        return false;
    }

    const wchar_t* languages[] = { L"ja", L"en", L"zh", L"ko", L"yue", L"auto" };
    for (const wchar_t* language : languages)
    {
        SendMessageW(_gpt_prompt_language_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(language));
        SendMessageW(_gpt_text_language_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(language));
    }
    SendMessageW(_gpt_prompt_language_combo, CB_SETCURSEL, 0, 0);
    SendMessageW(_gpt_text_language_combo, CB_SETCURSEL, 0, 0);

    SendMessageW(_volume_slider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(_volume_slider, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(_volume_slider, TBM_SETPOS, TRUE, 100);

    apply_control_font(_engine_select_label);
    apply_control_font(_engine_select_combo);
    apply_control_font(_gpt_host_label);
    apply_control_font(_gpt_host_edit);
    apply_control_font(_gpt_port_label);
    apply_control_font(_gpt_port_edit);
    apply_control_font(_gpt_reference_label);
    apply_control_font(_gpt_reference_edit);
    apply_control_font(_gpt_reference_browse_button);
    apply_control_font(_gpt_prompt_label);
    apply_control_font(_gpt_prompt_edit);
    apply_control_font(_gpt_prompt_language_label);
    apply_control_font(_gpt_prompt_language_combo);
    apply_control_font(_gpt_text_language_label);
    apply_control_font(_gpt_text_language_combo);
    apply_control_font(_gpt_settings_apply_button);
    apply_control_font(_engine_path_label);
    apply_control_font(_engine_path_edit);
    apply_control_font(_engine_path_browse_button);
    apply_control_font(_engine_path_apply_button);
    apply_control_font(_speaker_label);
    apply_control_font(_speaker_combo);
    apply_control_font(_reconnect_button);
    apply_control_font(_text_label);
    apply_control_font(_text_edit);
    apply_control_font(_volume_label);
    apply_control_font(_volume_test_button);
    apply_control_font(_play_button);
    apply_control_font(_status_text);
    apply_control_font(_speech_recognition_button);

    const int gpt_control_ids[] = {
        gpt_host_label_id, gpt_host_edit_id, gpt_port_label_id, gpt_port_edit_id,
        gpt_reference_label_id, gpt_reference_edit_id, gpt_reference_browse_button_id,
        gpt_prompt_label_id, gpt_prompt_edit_id, gpt_prompt_language_label_id,
        gpt_prompt_language_combo_id, gpt_text_language_label_id,
        gpt_text_language_combo_id, gpt_settings_apply_button_id
    };
    for (int control_id : gpt_control_ids)
    {
        ShowWindow(GetDlgItem(_window, control_id), SW_HIDE);
    }

    return true;
}

void main_window::layout_controls(int width, int height)
{
    if (!_engine_select_label || !_engine_select_combo || !_engine_path_label || !_engine_path_edit || !_engine_path_browse_button ||
        !_engine_path_apply_button || !_speaker_label || !_speaker_combo || !_reconnect_button ||
        !_text_label || !_text_edit || !_volume_label || !_volume_slider ||
        !_volume_test_button || !_play_button || !_status_text || !_speech_recognition_button)
    {
        return;
    }

    const int content_width = std::max(width - margin * 2, 100);
    const int buttons_y = height - margin - control_height;
    const int status_y = buttons_y + (control_height - label_height) / 2;
    const int volume_y = buttons_y - margin - control_height;
    const int volume_label_y = volume_y - label_height;
    const bool use_gpt_sovits = get_selected_engine() == 1;
    const int engine_select_y = margin + label_height;
    const int settings_top = engine_select_y + control_height + margin;
    const int path_y = settings_top + label_height;
    const int path_button_total_width = path_button_width * 2 + margin;
    const int speaker_label_y = use_gpt_sovits ? settings_top + 280 : path_y + label_height + control_height + margin;
    const int edit_y = speaker_label_y + label_height + control_height + margin + label_height;
    const int edit_height = std::max(volume_label_y - margin - edit_y - margin, 60);
    const int speaker_combo_width = std::max(content_width - reconnect_button_width - margin, 80);

    MoveWindow(_engine_select_label, margin, margin, content_width, label_height, TRUE);
    MoveWindow(_engine_select_combo, margin, engine_select_y, 220, control_height * 5, TRUE);

    const int half_width = std::max((content_width - margin) / 2, 100);
    const int small_label_width = 90;
    const int field_x = margin + small_label_width;
    const int field_width = std::max(half_width - small_label_width - margin, 80);
    MoveWindow(_gpt_host_label, margin, settings_top, small_label_width, control_height, TRUE);
    MoveWindow(_gpt_host_edit, field_x, settings_top, field_width, control_height, TRUE);
    MoveWindow(_gpt_port_label, margin + half_width + margin, settings_top, small_label_width, control_height, TRUE);
    MoveWindow(_gpt_port_edit, margin + half_width + margin + small_label_width, settings_top,
        std::max(half_width - small_label_width, 80), control_height, TRUE);

    const int reference_y = settings_top + control_height + margin;
    MoveWindow(_gpt_reference_label, margin, reference_y, small_label_width, control_height, TRUE);
    MoveWindow(_gpt_reference_edit, field_x, reference_y,
        std::max(content_width - small_label_width - path_button_width - margin, 100), control_height, TRUE);
    MoveWindow(_gpt_reference_browse_button, width - margin - path_button_width, reference_y,
        path_button_width, control_height, TRUE);

    const int prompt_label_y = reference_y + control_height + margin;
    const int prompt_y = prompt_label_y + label_height;
    MoveWindow(_gpt_prompt_label, margin, prompt_label_y, content_width, label_height, TRUE);
    MoveWindow(_gpt_prompt_edit, margin, prompt_y, content_width, 54, TRUE);

    const int language_y = prompt_y + 54 + margin;
    MoveWindow(_gpt_prompt_language_label, margin, language_y, small_label_width + 30, control_height, TRUE);
    MoveWindow(_gpt_prompt_language_combo, margin + small_label_width + 30, language_y, 100, control_height * 5, TRUE);
    MoveWindow(_gpt_text_language_label, margin + half_width + margin, language_y, small_label_width + 20, control_height, TRUE);
    MoveWindow(_gpt_text_language_combo, margin + half_width + margin + small_label_width + 20,
        language_y, 100, control_height * 5, TRUE);
    MoveWindow(_gpt_settings_apply_button, width - margin - 220, language_y + control_height + margin,
        220, control_height, TRUE);
    MoveWindow(_engine_path_label, margin, path_y, content_width, label_height, TRUE);
    MoveWindow(_engine_path_edit, margin, path_y + label_height, std::max(content_width - path_button_total_width - margin, 100), control_height, TRUE);
    MoveWindow(_engine_path_browse_button, width - margin - path_button_total_width, path_y + label_height, path_button_width, control_height, TRUE);
    MoveWindow(_engine_path_apply_button, width - margin - path_button_width, path_y + label_height, path_button_width, control_height, TRUE);

    MoveWindow(_speaker_label, margin, speaker_label_y, content_width, label_height, TRUE);
    MoveWindow(_speaker_combo, margin, speaker_label_y + label_height, speaker_combo_width, 240, TRUE);
    MoveWindow(
        _reconnect_button,
        margin + speaker_combo_width + margin,
        speaker_label_y + label_height,
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
    constexpr int speech_button_width = 110;
    const int speech_button_x = width - margin - play_button_width - margin - speech_button_width;
    MoveWindow(
        _speech_recognition_button,
        speech_button_x,
        buttons_y,
        speech_button_width,
        control_height,
        TRUE
    );

    const int status_x = margin;
    const int status_width = std::max(
        speech_button_x - margin - status_x,
        80
    );
    MoveWindow(_status_text, status_x, status_y, status_width, label_height, TRUE);
}

void main_window::update_engine_controls()
{
    const bool use_gpt_sovits = get_selected_engine() == 1;
    const int gpt_control_ids[] = {
        gpt_host_label_id, gpt_host_edit_id, gpt_port_label_id, gpt_port_edit_id,
        gpt_reference_label_id, gpt_reference_edit_id, gpt_reference_browse_button_id,
        gpt_prompt_label_id, gpt_prompt_edit_id, gpt_prompt_language_label_id,
        gpt_prompt_language_combo_id, gpt_text_language_label_id,
        gpt_text_language_combo_id, gpt_settings_apply_button_id
    };
    for (int control_id : gpt_control_ids)
    {
        HWND control = GetDlgItem(_window, control_id);
        if (control)
        {
            ShowWindow(control, use_gpt_sovits ? SW_SHOW : SW_HIDE);
        }
    }

    const int voicevox_control_ids[] = {
        engine_path_label_id, engine_path_edit_id,
        engine_path_browse_button_id, engine_path_apply_button_id
    };
    for (int control_id : voicevox_control_ids)
    {
        HWND control = GetDlgItem(_window, control_id);
        if (control)
        {
            ShowWindow(control, use_gpt_sovits ? SW_HIDE : SW_SHOW);
        }
    }

    RECT client{};
    GetClientRect(_window, &client);
    layout_controls(client.right, client.bottom);
}

void main_window::browse_gpt_reference_audio()
{
    const HRESULT apartment_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize_apartment = SUCCEEDED(apartment_result);
    if (FAILED(apartment_result) && apartment_result != RPC_E_CHANGED_MODE)
    {
        set_status(L"ファイル選択ダイアログを初期化できませんでした。");
        return;
    }

    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(dialog.GetAddressOf())
    );
    if (SUCCEEDED(result))
    {
        const COMDLG_FILTERSPEC filters[] = {
            { L"Audio files", L"*.wav;*.mp3;*.flac;*.ogg" },
            { L"WAV files", L"*.wav" },
            { L"All files", L"*.*" }
        };
        dialog->SetFileTypes(3, filters);
        dialog->SetTitle(L"GPT-SoVITS の参照音声を選択");

        FILEOPENDIALOGOPTIONS options{};
        if (SUCCEEDED(dialog->GetOptions(&options)))
        {
            dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        }
        result = dialog->Show(_window);
    }

    if (SUCCEEDED(result))
    {
        Microsoft::WRL::ComPtr<IShellItem> item;
        result = dialog->GetResult(item.GetAddressOf());
        if (SUCCEEDED(result))
        {
            PWSTR selected_path = nullptr;
            result = item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
            if (SUCCEEDED(result) && selected_path)
            {
                SetWindowTextW(_gpt_reference_edit, selected_path);
                CoTaskMemFree(selected_path);
            }
        }
    }

    if (uninitialize_apartment)
    {
        CoUninitialize();
    }
}

void main_window::browse_voicevox_path()
{
    const HRESULT apartment_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize_apartment = SUCCEEDED(apartment_result);
    if (FAILED(apartment_result) && apartment_result != RPC_E_CHANGED_MODE)
    {
        set_status(L"ファイル選択ダイアログを初期化できませんでした。");
        return;
    }

    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(dialog.GetAddressOf())
    );

    if (SUCCEEDED(result))
    {
        const COMDLG_FILTERSPEC filters[] = {
            { L"VOICEVOX Engine (run.exe)", L"run.exe" },
            { L"Executable files", L"*.exe" },
            { L"All files", L"*.*" }
        };
        dialog->SetFileTypes(3, filters);
        dialog->SetTitle(L"VOICEVOX Engine の run.exe を選択");

        FILEOPENDIALOGOPTIONS options{};
        if (SUCCEEDED(dialog->GetOptions(&options)))
        {
            dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        }

        result = dialog->Show(_window);
    }

    if (SUCCEEDED(result))
    {
        Microsoft::WRL::ComPtr<IShellItem> item;
        result = dialog->GetResult(item.GetAddressOf());
        if (SUCCEEDED(result))
        {
            PWSTR selected_path = nullptr;
            result = item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
            if (SUCCEEDED(result) && selected_path)
            {
                SetWindowTextW(_engine_path_edit, selected_path);
                CoTaskMemFree(selected_path);
                if (_voicevox_path_callback)
                {
                    _voicevox_path_callback(get_voicevox_path());
                }
            }
        }
    }

    if (uninitialize_apartment)
    {
        CoUninitialize();
    }
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
