#include "app/app.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellscalingapi.h>
#include <cstring>

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR cmdline, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    bool verify = (cmdline && wcsstr(cmdline, L"--verify") != nullptr);

    int code = 0;
    {
        np::App app;
        if (app.init(hinst, verify)) {
            code = app.run();
        } else {
            code = 1;
        }
    }

    CoUninitialize();
    return code;
}
