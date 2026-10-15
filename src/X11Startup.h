// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace qfr
{
// JUCE expects the window manager's WM_PROTOCOLS atom to exist even when the
// process is running on a bare X server such as the Xvfb used by CI.
void prepareX11ForJuce();
}
