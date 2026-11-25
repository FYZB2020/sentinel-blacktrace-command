// Sentinel Defense Grid -- Win32 entry point.
#include "app.h"

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    App app;
    return app.Run(hInst, nCmdShow);
}
