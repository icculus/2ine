#define INCL_WININPUT 1
#include <os2.h>

static MRESULT APIENTRY winproc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {
        case WM_PAINT: {
            RECTL rect;
            HPS hps = WinBeginPaint(hwnd, NULLHANDLE, &rect);
            WinFillRect(hps, &rect, CLR_WHITE);
            WinEndPaint(hps);
            return 0;
        }

        case WM_BUTTON1CLICK: {  // kill the app when the window is clicked on.
            WinPostMsg(hwnd, WM_QUIT, 0, 0);
            return 0;
        }
    }

    return WinDefWindowProc(hwnd, msg, mp1, mp2);
} // winproc

int main(int argc, char **argv)
{
    HAB hab = WinInitialize(0);
    HMQ hmq = WinCreateMsgQueue(hab, 0);
    QMSG qmsg;
    HWND hwnd;

    WinRegisterClass(hab, "testpm", winproc, 0, 0);
    hwnd = WinCreateWindow(HWND_DESKTOP, "testpm", "testpm", WS_VISIBLE, 100, 100, 100, 100, 0, HWND_TOP, 0, NULL, NULL);

    while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0)) {
        WinDispatchMsg(hab, &qmsg);
    }

    WinDestroyWindow(hwnd);
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);

    return 0;
} // main

// end of testpm.c ...

