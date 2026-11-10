#pragma once

#include <string>

class app_settings
{
public:
    bool load();
    bool save() const;

    const std::wstring& get_voicevox_path() const;
    void set_voicevox_path(const std::wstring& path);

private:
    std::wstring get_config_path() const;

    std::wstring _voicevox_path;
};
