#define NOMINMAX
#include <Windows.h>
#include <initguid.h>
#include <iostream>
#include <algorithm>
#include <string>
#include "DualCapture.h"
#include "Transcriber.h"

static HRESULT InitAudioClient(
    EDataFlow flow, DWORD sampleRate,
    IAudioClient** ppClient, IAudioCaptureClient** ppCapture, HANDLE hEvent)
{
    wil::com_ptr_nothrow<IMMDeviceEnumerator> pEnum;
    RETURN_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&pEnum)));

    wil::com_ptr_nothrow<IMMDevice> pDevice;
    RETURN_IF_FAILED(pEnum->GetDefaultAudioEndpoint(flow, eConsole, &pDevice));

    wil::com_ptr_nothrow<IAudioClient> pClient;
    RETURN_IF_FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pClient));

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 1;
    wfx.nSamplesPerSec  = sampleRate;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = 2;
    wfx.nAvgBytesPerSec = sampleRate * 2;

    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    if (flow == eRender)
        flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

    RETURN_IF_FAILED(pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &wfx, nullptr));
    RETURN_IF_FAILED(pClient->SetEventHandle(hEvent));

    wil::com_ptr_nothrow<IAudioCaptureClient> pCapture;
    RETURN_IF_FAILED(pClient->GetService(IID_PPV_ARGS(&pCapture)));

    *ppClient  = pClient.detach();
    *ppCapture = pCapture.detach();
    return S_OK;
}

HRESULT CDualCapture::InitSinkWriter(PCWSTR outputFile)
{
    RETURN_IF_FAILED(MFCreateSinkWriterFromURL(outputFile, nullptr, nullptr, &m_pSinkWriter));

    wil::com_ptr_nothrow<IMFMediaType> pOut;
    RETURN_IF_FAILED(MFCreateMediaType(&pOut));
    RETURN_IF_FAILED(pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    RETURN_IF_FAILED(pOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
    RETURN_IF_FAILED(pOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
    RETURN_IF_FAILED(pOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_nSampleRate));
    RETURN_IF_FAILED(pOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
    RETURN_IF_FAILED(pOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 6000));
    RETURN_IF_FAILED(m_pSinkWriter->AddStream(pOut.get(), &m_dwStreamIndex));

    wil::com_ptr_nothrow<IMFMediaType> pIn;
    RETURN_IF_FAILED(MFCreateMediaType(&pIn));
    RETURN_IF_FAILED(pIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    RETURN_IF_FAILED(pIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
    RETURN_IF_FAILED(pIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
    RETURN_IF_FAILED(pIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_nSampleRate));
    RETURN_IF_FAILED(pIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
    RETURN_IF_FAILED(m_pSinkWriter->SetInputMediaType(m_dwStreamIndex, pIn.get(), nullptr));

    return m_pSinkWriter->BeginWriting();
}

HRESULT CDualCapture::Drain(IAudioCaptureClient* pCapture, std::deque<short>& buf)
{
    UINT32 frames = 0;
    while (SUCCEEDED(pCapture->GetNextPacketSize(&frames)) && frames > 0)
    {
        BYTE* pData = nullptr;
        DWORD flags = 0;
        RETURN_IF_FAILED(pCapture->GetBuffer(&pData, &frames, &flags, nullptr, nullptr));
        auto release = wil::scope_exit([&] { pCapture->ReleaseBuffer(frames); });

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        {
            buf.insert(buf.end(), frames, (short)0);
        }
        else
        {
            auto* s = reinterpret_cast<const short*>(pData);
            buf.insert(buf.end(), s, s + frames);
        }
    }
    return S_OK;
}

HRESULT CDualCapture::MixAndWrite()
{
    size_t count = std::min(m_EndpointBuf.size(), m_MicBuf.size());
    if (count == 0) return S_OK;

    if (m_pTranscriber) {
        m_scratchEp.assign(m_EndpointBuf.begin(), m_EndpointBuf.begin() + count);
        m_scratchMic.assign(m_MicBuf.begin(),     m_MicBuf.begin()     + count);
        m_pTranscriber->Feed(m_scratchEp.data(), m_scratchMic.data(), count);
    }

    DWORD cbBytes = (DWORD)(count * 2 * sizeof(short));

    wil::com_ptr_nothrow<IMFSample> pSample;
    RETURN_IF_FAILED(MFCreateSample(&pSample));
    wil::com_ptr_nothrow<IMFMediaBuffer> pBuffer;
    RETURN_IF_FAILED(MFCreateMemoryBuffer(cbBytes, &pBuffer));

    BYTE* pDst = nullptr;
    RETURN_IF_FAILED(pBuffer->Lock(&pDst, nullptr, nullptr));
    auto* out = reinterpret_cast<short*>(pDst);
    for (size_t i = 0; i < count; i++)
    {
        out[i * 2]     = m_EndpointBuf.front(); m_EndpointBuf.pop_front();
        out[i * 2 + 1] = m_MicBuf.front();      m_MicBuf.pop_front();
    }
    pBuffer->Unlock();

    RETURN_IF_FAILED(pBuffer->SetCurrentLength(cbBytes));
    RETURN_IF_FAILED(pSample->AddBuffer(pBuffer.get()));

    LONGLONG llDuration = (LONGLONG)count * 10000000LL / m_nSampleRate;
    RETURN_IF_FAILED(pSample->SetSampleTime(m_llTimestamp));
    RETURN_IF_FAILED(pSample->SetSampleDuration(llDuration));
    m_llTimestamp += llDuration;

    return m_pSinkWriter->WriteSample(m_dwStreamIndex, pSample.get());
}

HRESULT CDualCapture::Start(DWORD seconds, PCWSTR outputFile)
{
    RETURN_IF_FAILED(MFStartup(MF_VERSION, 0));
    auto mfCleanup = wil::scope_exit([] { MFShutdown(); });

    {
        wil::com_ptr_nothrow<IMMDeviceEnumerator> pEnum;
        RETURN_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
            CLSCTX_ALL, IID_PPV_ARGS(&pEnum)));
        wil::com_ptr_nothrow<IMMDevice> pDevice;
        RETURN_IF_FAILED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice));
        wil::com_ptr_nothrow<IAudioClient> pTmp;
        RETURN_IF_FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pTmp));
        WAVEFORMATEX* pFmt = nullptr;
        RETURN_IF_FAILED(pTmp->GetMixFormat(&pFmt));
        m_nSampleRate = pFmt->nSamplesPerSec;
        CoTaskMemFree(pFmt);
    }
    std::wcout << L"  " << m_nSampleRate << L" Hz mono per channel -> stereo AAC 48 kbps\n";

    std::wstring transcriptPath = outputFile;
    auto dot = transcriptPath.rfind(L'.');
    if (dot != std::wstring::npos) transcriptPath.resize(dot);
    transcriptPath += L".txt";

    CTranscriber transcriber;
    bool transcribe = transcriber.Init(transcriptPath.c_str(), m_nSampleRate, seconds);
    if (transcribe) m_pTranscriber = &transcriber;
    else std::wcout << L"  Whisper model not found; skipping transcription.\n";

    RETURN_IF_FAILED(m_EndpointEvent.create(wil::EventOptions::None));
    RETURN_IF_FAILED(m_MicEvent.create(wil::EventOptions::None));

    RETURN_IF_FAILED(InitAudioClient(eRender,  m_nSampleRate,
        &m_EndpointClient, &m_EndpointCapture, m_EndpointEvent.get()));
    RETURN_IF_FAILED(InitAudioClient(eCapture, m_nSampleRate,
        &m_MicClient,      &m_MicCapture,      m_MicEvent.get()));
    RETURN_IF_FAILED(InitSinkWriter(outputFile));

    RETURN_IF_FAILED(m_EndpointClient->Start());
    RETURN_IF_FAILED(m_MicClient->Start());

    HANDLE events[2] = { m_EndpointEvent.get(), m_MicEvent.get() };
    ULONGLONG endTime = GetTickCount64() + (ULONGLONG)seconds * 1000;

    while (GetTickCount64() < endTime)
    {
        WaitForMultipleObjects(2, events, FALSE, 50);
        RETURN_IF_FAILED(Drain(m_EndpointCapture.get(), m_EndpointBuf));
        RETURN_IF_FAILED(Drain(m_MicCapture.get(),      m_MicBuf));
        RETURN_IF_FAILED(MixAndWrite());
    }

    m_EndpointClient->Stop();
    m_MicClient->Stop();

    size_t tail = std::max(m_EndpointBuf.size(), m_MicBuf.size());
    while (m_EndpointBuf.size() < tail) m_EndpointBuf.push_back(0);
    while (m_MicBuf.size()      < tail) m_MicBuf.push_back(0);
    RETURN_IF_FAILED(MixAndWrite());

    RETURN_IF_FAILED(m_pSinkWriter->Finalize());

    m_pTranscriber = nullptr;
    if (transcribe) {
        std::wcout << L"  Transcribing...\n";
        transcriber.Finalize();
        std::wcout << L"  Transcript: " << transcriptPath << L"\n";
    }

    return S_OK;
}
