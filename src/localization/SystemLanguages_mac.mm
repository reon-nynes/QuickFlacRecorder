// SPDX-License-Identifier: AGPL-3.0-only
#include "SystemLanguages.h"
#import <Foundation/Foundation.h>

namespace qfr::i18n
{
juce::StringArray nativeMacLanguagePreferences()
{
    @autoreleasepool
    {
        juce::StringArray preferred;
        for (NSString* language in [NSLocale preferredLanguages])
            preferred.add(juce::String::fromUTF8([language UTF8String]));
        return macLanguagePreferences(preferred, true);
    }
}
}
