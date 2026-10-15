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
    if (auto* display = XOpenDisplay(nullptr))
    {
        // JUCE 9.0.3 looks this atom up with only_if_exists=true, but later
        // writes the property without checking for None. A bare Xvfb server
        // has no window manager to create it for us.
        XInternAtom(display, "WM_PROTOCOLS", False);
        XFlush(display);
        XCloseDisplay(display);
    }
#endif
}
}
