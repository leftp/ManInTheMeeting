#include "Transcriber.h"
#include "resource.h"
#include <whisper.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

static whisper_context* LoadModelFromResource()
{
    HRSRC hRes = FindResource(nullptr, MAKEINTRESOURCE(IDR_WHISPER_MODEL), RT_RCDATA);
    if (!hRes) return nullptr;
    HGLOBAL hGlobal = LoadResource(nullptr, hRes);
    if (!hGlobal) return nullptr;
    void*  pData  = LockResource(hGlobal);
    DWORD  cbSize = SizeofResource(nullptr, hRes);
    if (!pData || cbSize == 0) return nullptr;

    whisper_context_params cparams = {};
    cparams.use_gpu = false;
    return whisper_init_from_buffer_with_params(pData, cbSize, cparams);
}

bool CTranscriber::Init(PCWSTR logPath, DWORD srcSampleRate, DWORD durationSeconds)
{
    m_logPath = logPath;
    m_srcRate = srcSampleRate;
    HRSRC hRes = FindResource(nullptr, MAKEINTRESOURCE(IDR_WHISPER_MODEL), RT_RCDATA);
    if (!hRes || SizeofResource(nullptr, hRes) == 0) return false;
    m_remote.reserve((size_t)16000 * durationSeconds);
    m_mic.reserve((size_t)16000 * durationSeconds);
    return true;
}

void CTranscriber::Feed(const short* remote, const short* mic, size_t frames)
{
    if (frames == 0) return;
    const double ratio = (double)m_srcRate / 16000.0;
    double pos = m_resPos;
    while (pos < (double)frames) {
        size_t i    = (size_t)pos;
        double frac = pos - (double)i;
        float r, m;
        if (i + 1 < frames) {
            r = (float)(remote[i] * (1.0 - frac) + remote[i + 1] * frac) / 32768.f;
            m = (float)(mic[i]    * (1.0 - frac) + mic[i + 1]    * frac) / 32768.f;
        } else {
            r = (float)remote[i] / 32768.f;
            m = (float)mic[i]    / 32768.f;
        }
        m_remote.push_back(r);
        m_mic.push_back(m);
        pos += ratio;
    }
    m_resPos = pos - (double)frames;
}

void CTranscriber::Transcribe(
    const std::vector<float>& pcm, bool isMic,
    std::vector<std::pair<int64_t, std::string>>& out)
{
    unsigned hw = std::thread::hardware_concurrency();
    whisper_full_params params   = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language              = "en";
    params.n_threads             = hw ? (int)hw : 4;
    params.no_context            = true;
    params.print_progress        = false;
    params.print_realtime        = false;
    params.print_timestamps      = false;

    if (whisper_full(m_ctx, params, pcm.data(), (int)pcm.size()) != 0)
        return;

    const char* label = isMic ? "[MIC]   " : "[REMOTE]";
    int n = whisper_full_n_segments(m_ctx);
    for (int i = 0; i < n; i++) {
        const char* text = whisper_full_get_segment_text(m_ctx, i);
        if (!text || text[0] == '\0') continue;

        if (strstr(text, "[BLANK_AUDIO]") || strstr(text, "[ MUSIC]") ||
            strstr(text, "[MUSIC]")       || strstr(text, "[ NOISE]") ||
            strstr(text, "[NOISE]"))
            continue;

        int64_t t0 = whisper_full_get_segment_t0(m_ctx, i);
        int64_t t1 = whisper_full_get_segment_t1(m_ctx, i);
        size_t  s0 = (size_t)(t0 * 16000 / 100);
        size_t  s1 = (size_t)(t1 * 16000 / 100);
        if (s1 > pcm.size()) s1 = pcm.size();
        if (s1 > s0) {
            float sum = 0.0f;
            for (size_t j = s0; j < s1; j++) sum += pcm[j] * pcm[j];
            float rms = sqrtf(sum / (float)(s1 - s0));
            if (rms < 0.01f) continue;
        }

        int64_t t0ms = t0 * 10;
        out.push_back({ t0ms, std::string(label) + text });
    }
}

void CTranscriber::Finalize()
{
    if (m_remote.empty()) return;
    if (!m_ctx) m_ctx = LoadModelFromResource();
    if (!m_ctx) return;

    std::vector<std::pair<int64_t, std::string>> segs;
    Transcribe(m_remote, false, segs);
    Transcribe(m_mic,    true,  segs);

    std::stable_sort(segs.begin(), segs.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    FILE* f = nullptr;
    if (_wfopen_s(&f, m_logPath.c_str(), L"w") != 0 || !f) return;
    for (const auto& s : segs) {
        int64_t ms = s.first;
        int hh = (int)(ms / 3600000);
        int mm = (int)((ms % 3600000) / 60000);
        int ss = (int)((ms % 60000) / 1000);
        int cc = (int)((ms % 1000) / 10);
        fprintf(f, "[%02d:%02d:%02d.%02d] %s\n", hh, mm, ss, cc, s.second.c_str());
    }
    fclose(f);
}

CTranscriber::~CTranscriber()
{
    if (m_ctx) whisper_free(m_ctx);
}
