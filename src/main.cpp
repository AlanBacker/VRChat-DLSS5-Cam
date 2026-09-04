// VRChat DLSS5 Cam - entry point.
#include "core/App.h"
#include <exception>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    try {
        vdc::App app;
        return app.Run(hInstance, nCmdShow);
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "VRChat DLSS5 Cam", MB_ICONERROR | MB_OK);
        return 1;
    }
}
