#pragma once

#include <atomic>
#include <functional>
#include <string>

class speech_recognition_engine
{
public:
    virtual ~speech_recognition_engine() = default;

    virtual bool run(
        const std::atomic_bool& stop_requested,
        const std::function<void(const std::wstring&)>& on_recognized,
        std::wstring& error
    ) = 0;
};
