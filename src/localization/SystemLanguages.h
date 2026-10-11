// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <span>

namespace qfr::i18n
{
// Platform-boundary inputs are injectable; tests never change real user settings.
juce::StringArray windowsLanguagePreferences(std::span<const wchar_t> multiString, bool succeeded);
juce::StringArray macLanguagePreferences(const juce::StringArray& preferred, bool succeeded);
juce::StringArray linuxLanguagePreferences(const std::function<juce::String(const char*)>& environment);
juce::StringArray systemLanguagePreferences();
}
