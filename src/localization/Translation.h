// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <variant>

namespace qfr::i18n
{
enum class MessageId
{
   #define QFR_MESSAGE(id, key, arguments, context) id,
   #include "Messages.def"
   #undef QFR_MESSAGE
};

enum class PluralRule { oneOther, other };

struct Language
{
    juce::String tag, name;
    juce::StringArray aliases;
    juce::String resource;
    PluralRule pluralRule = PluralRule::other;
};

// Catalog text is owned by this value, including for test-only registrations.
struct Catalog
{
    Language language;
    juce::String json;
};

struct Argument
{
    juce::String name;
    std::variant<juce::String, double> value;
    Argument(juce::String key, juce::String text) : name(std::move(key)), value(std::move(text)) {}
    Argument(juce::String key, double number) : name(std::move(key)), value(number) {}
};

juce::String normalizeLanguageTag(const juce::String&);
Language resolveLanguage(const juce::StringArray& preferences, const std::vector<Language>& registry);
std::vector<Catalog> productionCatalogs();
juce::StringArray validateCatalog(const Catalog&);
juce::StringArray validateProductionCatalogs();

// Immutable after construction. All resource access and compilation happens here,
// before presentation is built. Formatting never reads the operating system.
class Translation final
{
public:
    explicit Translation(const juce::StringArray& preferences,
                         std::vector<Catalog> catalogs = productionCatalogs());
    const Language& language() const;
    juce::String text(MessageId, std::initializer_list<Argument> arguments = {}) const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl;
};
}
