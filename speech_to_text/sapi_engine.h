#pragma once

#include "speech_recognition_engine.h"

class sapi_engine : public speech_recognition_engine
{
public:
    bool run(
        const std::atomic_bool& stop_requested,
        const std::function<void(const std::wstring&)>& on_recognized,
        std::wstring& error
    ) override;
};
