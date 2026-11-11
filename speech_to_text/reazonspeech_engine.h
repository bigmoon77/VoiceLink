#pragma once

#include <memory>
#include <string>
#include <vector>

class reazonspeech_engine
{
public:
    reazonspeech_engine();
    ~reazonspeech_engine();

    reazonspeech_engine(const reazonspeech_engine&) = delete;
    reazonspeech_engine& operator=(const reazonspeech_engine&) = delete;

    bool initialize(std::wstring& error);

    bool recognize(
        const std::vector<float>& samples,
        int sample_rate,
        std::wstring& text,
        std::wstring& error
    ) const;

    bool is_initialized() const;

private:
    struct implementation;
    std::unique_ptr<implementation> _implementation;
};
