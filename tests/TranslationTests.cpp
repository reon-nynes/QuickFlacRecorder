// SPDX-License-Identifier: AGPL-3.0-only
#include "localization/Translation.h"
#include "localization/SystemLanguages.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (! value) throw std::runtime_error(message);
}
void resolvesOrderedInterfacePreferences()
{
    using namespace qfr::i18n;
    require(Translation({ "fr-FR", "ZH_hant_TW", "en-US" }).language().tag == "zh-Hans",
            "next supported preference selects the sole Chinese catalog");
    require(Translation({ "en-GB", "zh-CN" }).language().tag == "en", "preference order is respected");
    for (const auto& preferences : std::vector<juce::StringArray> { {}, { "fr-FR" }, { "???", "en--US", "zh-" } })
        require(Translation(preferences).language().tag == "en", "unsupported/empty/malformed detection falls back to English");
    require(normalizeLanguageTag("ZH_hant_tw") == "zh-Hant-TW", "script and region survive normalization");
    for (const auto& malformed : { "zh-a", "zh-12", "zh-Hans-Hant", "zh-CN-@bad", "zh-x", "zh--CN" })
        require(Translation({ malformed }).language().tag == "en", "malformed Chinese tags cannot select a catalog");
    auto catalogs = productionCatalogs();
    catalogs.push_back({ { "zh-Hant", "Traditional Chinese", {}, "test-only", PluralRule::other }, "{}" });
    require(Translation({ "zh-Hant-TW" }, catalogs).language().tag == "zh-Hant", "future script matches precede generic Chinese fallback");
    for (const auto& preference : { "zh-x-hant", "zh-u-hant" })
        require(Translation({ preference }, catalogs).language().tag == "zh-Hans", "extension and private-use values are not scripts");
    catalogs.push_back({ { "zh-Hant-TW", "Taiwan", {}, "test-only", PluralRule::other }, "{}" });
    require(Translation({ "zh-Hant-TW" }, catalogs).language().tag == "zh-Hant-TW", "exact region matches precede script matches");
    catalogs = productionCatalogs();
    catalogs.push_back({ { "zh-TW", "Traditional Chinese", { "ZH_hant" }, "test-only", PluralRule::other }, "{}" });
    require(Translation({ "zh-Hant-HK" }, catalogs).language().tag == "zh-TW", "normalized script aliases precede generic Chinese fallback");
    catalogs.push_back({ { "zh-HK", "Hong Kong", { "zh-Hant-HK" }, "test-only", PluralRule::other }, "{}" });
    require(Translation({ "zh-Hant-HK" }, catalogs).language().tag == "zh-HK", "exact aliases precede script aliases");
}
void readsAdapterInputsWithoutChangingUserSettings()
{
    using namespace qfr::i18n;
    const wchar_t windows[] = L"fr-FR\0zh-Hant-TW\0en-US\0";
    require(Translation(windowsLanguagePreferences(windows, true)).language().tag == "zh-Hans", "Windows ordered multistring");
    require(windowsLanguagePreferences(windows, false).isEmpty(), "failed Windows detection is empty");
    const wchar_t truncated[] = { L'z', L'h' };
    require(windowsLanguagePreferences(truncated, true).isEmpty(), "unterminated Windows detection is rejected");
    require(Translation(macLanguagePreferences({ "fr", "zh_Hans_CN", "en" }, true)).language().tag == "zh-Hans", "macOS ordered preferred languages");
    require(macLanguagePreferences({ "zh" }, false).isEmpty(), "failed macOS detection is empty");
    std::map<juce::String, juce::String> env { { "LANG", "en_US.UTF-8" }, { "LANGUAGE", "fr:zh_TW:en_GB" } };
    const auto read = [&env](const char* key) { return env[key]; };
    require(Translation(linuxLanguagePreferences(read)).language().tag == "zh-Hans", "Linux LANGUAGE order");
    env["LC_MESSAGES"] = "zh_CN.UTF-8"; env["LANGUAGE"] = "";
    require(Translation(linuxLanguagePreferences(read)).language().tag == "zh-Hans", "LC_MESSAGES overrides LANG");
    env["LC_ALL"] = "en_GB.UTF-8";
    require(Translation(linuxLanguagePreferences(read)).language().tag == "en", "LC_ALL overrides LC_MESSAGES");
    env["LANGUAGE"] = "zh";
    for (const auto& locale : { "C", "POSIX", "C.UTF-8" })
    {
        env["LC_ALL"] = locale;
        require(Translation(linuxLanguagePreferences(read)).language().tag == "en", "effective C/POSIX disables LANGUAGE");
    }
    env.clear(); env["LANGUAGE"] = "zh";
    require(Translation(linuxLanguagePreferences(read)).language().tag == "en", "unset message locale is effectively C");
    env["LANG"] = "zh_TW.UTF-8@modifier";
    require(linuxLanguagePreferences(read)[0] == "zh", "LANGUAGE remains first in non-C locale");
    const Translation first(macLanguagePreferences({ "en" }, true));
    const Translation next(macLanguagePreferences({ "zh" }, true));
    require(first.language().tag == "en" && next.language().tag == "zh-Hans", "new launch re-queries; an existing selection remains immutable");
}
void formatsCompleteMessagesAndFallsBack()
{
    using namespace qfr::i18n;
    require(validateProductionCatalogs().isEmpty(), "all registered production catalogs validate");
    const Translation english({ "en-US" }), chinese({ "zh-TW" });
    require(english.text(MessageId::Format, { { "channels", 1.0 }, { "rate", juce::String("48000 Hz") } })
                == "48000 Hz  /  16-bit FLAC  /  1 channel", "English singular uses named values");
    require(english.text(MessageId::Format, { { "rate", juce::String("96000 Hz") }, { "channels", 2.0 } })
                == "96000 Hz  /  16-bit FLAC  /  2 channels", "English plural uses locale rule");
    for (const auto& [number, expected] : std::vector<std::pair<double, juce::String>> {
             { 1000001.0, "1000001" }, { 1.0000001, "1.0000001" }, { 0.0000001, "1e-07" } })
        require(english.text(MessageId::Format, { { "rate", juce::String("48000 Hz") }, { "channels", number } })
                    == "48000 Hz  /  16-bit FLAC  /  " + expected + " channels", "numeric arguments preserve their value without rounding");
    require(chinese.text(MessageId::Format, { { "rate", juce::String("48000 Hz") }, { "channels", 1.0 } })
                == juce::String::fromUTF8("48000 Hz  /  16 位 FLAC  /  1 声道"), "Chinese uses other for singular numbers");
    const auto filename = juce::String::fromUTF8("原始前缀_01.flac");
    require(chinese.text(MessageId::Saved, { { "filename", filename } }) == juce::String::fromUTF8("已保存：") + filename,
            "saved filename is substituted verbatim");
    auto catalogs = productionCatalogs();
    const Language extra { "en-XA", "Expanded test language", {}, "test-only", PluralRule::oneOther };
    catalogs.push_back({ extra, R"({"format":"{channels, plural, one {# recorded channel} other {# recorded channels}} at the original stream rate {rate}","saved":"The original recording filename was {filename}. It has finished saving successfully."})" });
    const Translation expanded({ "en-XA" }, catalogs);
    require(expanded.text(MessageId::Format, { { "rate", juce::String("48000 Hz") }, { "channels", 2.0 } })
                == "2 recorded channels at the original stream rate 48000 Hz", "third registration reorders arguments without caller changes");
    require(expanded.text(MessageId::Record) == "Record", "absent translated entry falls back to the complete English message");
    for (const auto& invalid : { R"({"saved":"Saved {wrong}"})", R"({"saved":"Saved {filename"})",
                                 R"({"saved":"Saved {filename, plural, other {#}}"})", R"({"saved":17})" })
    {
        catalogs.back().json = invalid;
        const Translation broken({ "en-XA" }, catalogs);
        require(broken.text(MessageId::Saved, { { "filename", filename } }) == "Saved " + filename,
                "unusable translated entry falls back as a whole with original arguments");
    }
    require(english.text(MessageId::MeterDescription, { { "right", juce::String("-12.0") }, { "mode", juce::String("stereo") }, { "left", juce::String("-6.0") } })
                == "Left input: -6.0 dBFS, right input: -12.0 dBFS", "select formats a complete message");
    bool rejected = false;
    try { english.text(MessageId::Saved, { { "filename", 2.0 } }); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "incorrect named argument type fails at the public boundary");
}
void rejectsInvalidCatalogsAndTemplates()
{
    using namespace qfr::i18n;
    const auto language = productionCatalogs()[0].language;
    for (const auto& json : { "{}", R"({"saved":"{filename}","saved":"duplicate {filename}"})",
         R"({"saved":"{filename}","\u0073aved":"escaped duplicate {filename}"})",
         R"({"unknown":"Unused"})", R"({"format":"{channels, plural, one {# channel}} {rate}"})",
         R"({"meter_description":"{mode, select, mono {{left} {right}}}"})",
         R"({"saved":"Saved {filename}}"})", R"({"saved":"Saved"})", R"({"saved":false})", R"({"saved":"\q {filename}"})" })
        require(! validateCatalog({ language, json }).isEmpty(), "invalid production catalog is rejected");
    auto catalogs = productionCatalogs();
    catalogs.push_back({ { "en-XA", "Test", {}, "test", PluralRule::oneOther },
        R"({"format":"{channels, plural, =0 {No channels at {rate}} =1000001 {Exact large count at {rate}} =0002 {Exact padded count at {rate}} one {One channel at {rate}} other {# channels at {rate}}}"})" });
    require(Translation({ "en-XA" }, catalogs).text(MessageId::Format, { { "rate", juce::String("48000 Hz") }, { "channels", 0.0 } })
            == "No channels at 48000 Hz", "exact plural overrides locale category");
    require(Translation({ "en-XA" }, catalogs).text(MessageId::Format, { { "rate", juce::String("48000 Hz") }, { "channels", 1000001.0 } })
            == "Exact large count at 48000 Hz", "exact plural matches unrounded large values");
    require(Translation({ "en-XA" }, catalogs).text(MessageId::Format, { { "rate", juce::String("48000 Hz") }, { "channels", 2.0 } })
            == "Exact padded count at 48000 Hz", "exact plural compares numeric values");
    catalogs.back().language.pluralRule = PluralRule::other;
    require(Translation({ "en-XA" }, catalogs).text(MessageId::Format, { { "rate", juce::String("48000 Hz") }, { "channels", 1.0 } })
            == "1 channels at 48000 Hz", "plural rule comes from the registered locale, not English grammar");
}
}
int main()
{
    try
    {
        resolvesOrderedInterfacePreferences();
        std::cout << "PASS ordered interface-language resolution\n";
        readsAdapterInputsWithoutChangingUserSettings();
        std::cout << "PASS platform adapter inputs and subsequent launches\n";
        formatsCompleteMessagesAndFallsBack();
        std::cout << "PASS catalogs, named templates, plural/select, extension and English fallback\n";
        rejectsInvalidCatalogsAndTemplates();
        std::cout << "PASS duplicate/completeness/syntax/type/default validation\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
