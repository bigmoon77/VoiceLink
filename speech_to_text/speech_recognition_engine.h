#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct speech_recognition_result
{
    std::uint64_t id = 0;
    std::wstring text;
    std::vector<std::wstring> alternatives;
};

class speech_recognition_engine
{
public:
    virtual ~speech_recognition_engine() = default;

    virtual bool supports_training() const
    {
        return false;
    }

    virtual bool run(
        const std::atomic_bool& stop_requested,
        const std::function<void(const speech_recognition_result&)>& on_recognized,
        const std::function<void(const std::wstring&)>& on_status,
        std::wstring& error
    ) = 0;

    virtual bool select_alternate(std::uint64_t, std::size_t)
    {
        return false;
    }

    virtual bool submit_adaptation_data(const std::wstring&)
    {
        return false;
    }

    virtual bool submit_adaptation_batch(const std::vector<std::wstring>&, std::size_t)
    {
        return false;
    }
};
