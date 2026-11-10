#pragma once

#include "speech_recognition_engine.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <sapi.h>
#include <wrl/client.h>

class sapi_engine : public speech_recognition_engine
{
public:
    sapi_engine();
    ~sapi_engine() override;

    sapi_engine(const sapi_engine&) = delete;
    sapi_engine& operator=(const sapi_engine&) = delete;

    bool supports_training() const override;
    bool run(
        const std::atomic_bool& stop_requested,
        const std::function<void(const speech_recognition_result&)>& on_recognized,
        const std::function<void(const std::wstring&)>& on_status,
        std::wstring& error
    ) override;
    bool select_alternate(std::uint64_t result_id, std::size_t alternate_index) override;
    bool submit_adaptation_data(const std::wstring& text) override;

private:
    enum class command_type
    {
        commit_alternate,
        adaptation_data
    };

    struct command
    {
        command_type type = command_type::adaptation_data;
        std::uint64_t result_id = 0;
        std::size_t alternate_index = 0;
        std::wstring text;
    };

    void enqueue_command(const command& value);

    HANDLE _command_event = nullptr;
    std::atomic_bool _running{ false };
    std::atomic_bool _adaptation_pending{ false };
    std::mutex _command_mutex;
    std::deque<command> _commands;
    std::map<std::uint64_t, std::vector<Microsoft::WRL::ComPtr<ISpPhraseAlt>>> _pending_alternates;
    std::uint64_t _next_result_id = 1;
};
