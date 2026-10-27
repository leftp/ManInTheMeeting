#include <Windows.h>
#include <iostream>
#include "DualCapture.h"

static void usage()
{
    std::wcout <<
        L"Usage:\n"
        L"  ManInTheMeeting <seconds> <outputfile.m4a>\n"
        L"      Capture remote audio (L channel) + microphone (R channel) as stereo AAC.\n";
}

int wmain(int argc, wchar_t* argv[])
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        std::wcout << L"CoInitializeEx failed: 0x" << std::hex << hr << L"\n";
        return 1;
    }
    auto comCleanup = wil::scope_exit([] { CoUninitialize(); });

    if (argc != 3) { usage(); return 0; }

    DWORD seconds = wcstoul(argv[1], nullptr, 10);
    if (seconds == 0) { usage(); return 0; }

    PCWSTR ext = wcsrchr(argv[2], L'.');
    if (!ext || (_wcsicmp(ext, L".m4a") != 0 && _wcsicmp(ext, L".mp4") != 0))
    {
        std::wcout << L"Output must be .m4a (or .mp4).\n";
        return 0;
    }

    std::wcout << L"Capturing " << seconds << L" seconds (remote L + mic R)...\n";
    CDualCapture dual;
    hr = dual.Start(seconds, argv[2]);
    if (FAILED(hr))
    {
        wil::unique_hlocal_string message;
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
            nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (PWSTR)&message, 0, nullptr);
        std::wcout << L"Failed: 0x" << std::hex << hr << L": " << message.get() << L"\n";
    }
    else
    {
        std::wcout << L"Finished. Output: " << argv[2] << L"\n";
    }
    return 0;
}
