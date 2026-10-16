// SPDX-License-Identifier: AGPL-3.0-only
#include "X11Startup.h"

#if defined(__linux__)
 #include <X11/Xlib.h>
#endif

namespace qfr
{
void prepareX11ForJuce()
{
#if defined(__linux__)
    // JUCE calls XInitThreads before it opens its own display connection. Do
    // the same before using Xlib here so that our preparation does not leave
    // JUCE with a partially initialised Xlib runtime.
    if (XInitThreads() == 0)
        return;

    // Keep this connection alive until process exit. The atoms are server
    // global, but retaining the connection also makes the ordering explicit:
    // JUCE cannot initialise its X11 backend until all names below have been
    // created and the requests have reached the server.
    static auto* display = XOpenDisplay(nullptr);
    if (display != nullptr)
    {
        // JUCE 9.0.3 looks these atoms up with only_if_exists=true, but later
        // uses some of them in XChangeProperty without checking for None. A
        // bare Xvfb server has no window manager to create them for us.
        static constexpr const char* lookupOnlyAtoms[] =
        {
            "WM_PROTOCOLS",
            "WM_TAKE_FOCUS",
            "WM_DELETE_WINDOW",
            "_NET_WM_PING",
            "WM_CHANGE_STATE",
            "WM_STATE",
            "_NET_WM_WINDOW_TYPE",
            "_NET_WM_WINDOW_TYPE_COMBO",
            "_NET_WM_WINDOW_TYPE_NORMAL",
            "_NET_WM_STATE",
            "_NET_WM_STATE_HIDDEN",
            "_NET_WM_STATE_MAXIMIZED_HORZ",
            "_NET_WM_STATE_MAXIMIZED_VERT",
            "_MOTIF_WM_HINTS",
            "_WIN_HINTS",
            "KWM_WIN_DECORATION",
            "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE",
            "_NET_WM_ALLOWED_ACTIONS",
            "_NET_WM_ACTION_RESIZE",
            "_NET_WM_ACTION_FULLSCREEN",
            "_NET_WM_ACTION_MINIMIZE",
            "_NET_WM_ACTION_CLOSE",
            "_NET_WM_STATE_SKIP_TASKBAR",
            "_NET_WM_STATE_ABOVE"
        };

        for (const auto* name : lookupOnlyAtoms)
            XInternAtom(display, name, False);

        XSync(display, False);
    }
#endif
}
}
