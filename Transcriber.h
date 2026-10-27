#pragma once
#include <Windows.h>
#include <string>
#include <vector>

struct whisper_context;

class CTranscriber {
public:
    bool Init(PCWSTR logPath, DWORD srcSampleRate, DWORD durationSeconds);
    void Feed(const short* remote, const short* mic, size_t frames);
    void Finalize();
    ~CTranscriber();

private:
    void Transcribe(const std::vector<float>& pcm, bool isMic,
                    std::vector<std::pair<int64_t, std::string>>& out);

    whisper_context* m_ctx     = nullptr;
    DWORD            m_srcRate = 0;
    double           m_resPos  = 0.0;

    std::vector<float> m_remote;
    std::vector<float> m_mic;
    std::wstring       m_logPath;
};
