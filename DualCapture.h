#pragma once
#include <AudioClient.h>
#include <mmdeviceapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wil/com.h>
#include <wil/result.h>
#include <wil/resource.h>
#include <deque>
#include <vector>

class CTranscriber;

class CDualCapture
{
public:
    HRESULT Start(DWORD seconds, PCWSTR outputFile);

private:
    HRESULT InitSinkWriter(PCWSTR outputFile);
    HRESULT Drain(IAudioCaptureClient* pCapture, std::deque<short>& buf);
    HRESULT MixAndWrite();

    wil::com_ptr_nothrow<IAudioClient>        m_EndpointClient;
    wil::com_ptr_nothrow<IAudioCaptureClient> m_EndpointCapture;
    wil::unique_event_nothrow                 m_EndpointEvent;

    wil::com_ptr_nothrow<IAudioClient>        m_MicClient;
    wil::com_ptr_nothrow<IAudioCaptureClient> m_MicCapture;
    wil::unique_event_nothrow                 m_MicEvent;

    wil::com_ptr_nothrow<IMFSinkWriter> m_pSinkWriter;
    DWORD    m_dwStreamIndex = 0;
    LONGLONG m_llTimestamp   = 0;
    DWORD    m_nSampleRate   = 0;

    std::deque<short> m_EndpointBuf;
    std::deque<short> m_MicBuf;

    std::vector<short> m_scratchEp;
    std::vector<short> m_scratchMic;

    CTranscriber* m_pTranscriber = nullptr;
};
