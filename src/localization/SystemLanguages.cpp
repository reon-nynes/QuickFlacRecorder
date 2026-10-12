// SPDX-License-Identifier: AGPL-3.0-only
#include "SystemLanguages.h"
#include "Translation.h"
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace qfr::i18n
{
namespace
{
juce::String localeTag(juce::String locale)
{
    return normalizeLanguageTag(locale.upToFirstOccurrenceOf(".", false, false)
                                      .upToFirstOccurrenceOf("@", false, false));
}
}
juce::StringArray windowsLanguagePreferences(std::span<const wchar_t> buffer, bool succeeded)
{
    juce::StringArray result;
    if (! succeeded || buffer.size() < 2 || buffer.back() != 0) return {};
    size_t start = 0;
    while (start < buffer.size())
    {
        auto end = start;
        while (end < buffer.size() && buffer[end] != 0) ++end;
        if (end == buffer.size()) return {};
        if (end == start) return result;
        const auto tag = normalizeLanguageTag(juce::String(buffer.data() + start, end - start));
        if (tag.isNotEmpty()) result.add(tag);
        start = end + 1;
    }
    return {}; // The ordered list must end with a second NUL.
}
juce::StringArray macLanguagePreferences(const juce::StringArray& preferred, bool succeeded)
{
    juce::StringArray result;
    if (succeeded) for (const auto& value : preferred)
    {
        const auto tag = normalizeLanguageTag(value);
        if (tag.isNotEmpty()) result.add(tag);
    }
    return result;
}
juce::StringArray linuxLanguagePreferences(const std::function<juce::String(const char*)>& environment)
{
    auto locale = environment("LC_ALL");
    if (locale.isEmpty()) locale = environment("LC_MESSAGES");
    if (locale.isEmpty()) locale = environment("LANG");
    const auto base = locale.upToFirstOccurrenceOf(".", false, false).upToFirstOccurrenceOf("@", false, false);
    if (locale.isEmpty() || base == "C" || base == "POSIX") return { "en" };
    juce::StringArray result;
    for (const auto& value : juce::StringArray::fromTokens(environment("LANGUAGE"), ":", ""))
    {
        // GNU LANGUAGE can use C as a stop/fallback entry in a non-C locale.
        const auto plain = value.upToFirstOccurrenceOf(".", false, false).upToFirstOccurrenceOf("@", false, false);
        if (plain == "C" || plain == "POSIX") { result.add("en"); break; }
        const auto tag = localeTag(value);
        if (tag.isNotEmpty()) result.add(tag);
    }
    const auto tag = localeTag(locale);
    if (tag.isNotEmpty()) result.add(tag);
    return result;
}
juce::StringArray systemLanguagePreferences()
{
   #if JUCE_WINDOWS
    ULONG count = 0, size = 0;
    if (! GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) || size < 2) return {};
    std::vector<wchar_t> buffer(size);
    if (! GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size)) return {};
    return windowsLanguagePreferences(std::span<const wchar_t>(buffer.data(), size), true);
   #elif JUCE_MAC
    // Implemented in Objective-C++ to read NSLocale.preferredLanguages.
    extern juce::StringArray nativeMacLanguagePreferences();
    return nativeMacLanguagePreferences();
   #elif JUCE_LINUX
    return linuxLanguagePreferences([](const char* key) { return juce::SystemStats::getEnvironmentVariable(key, {}); });
   #else
    return {};
   #endif
}
}
