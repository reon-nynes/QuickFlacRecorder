// SPDX-License-Identifier: AGPL-3.0-only
#include "Translation.h"
#include "TranslationResources.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace qfr::i18n
{
namespace
{
struct Definition { MessageId id; const char* key; const char* arguments; const char* context; };
const Definition definitions[] {
   #define QFR_MESSAGE(id, key, arguments, context) { MessageId::id, key, arguments, context },
   #include "Messages.def"
   #undef QFR_MESSAGE
};
enum class ArgumentKind { text, number };
using Schema = std::map<std::string, ArgumentKind>;
Schema schemaFor(const Definition& definition)
{
    Schema schema;
    for (const auto& argument : juce::StringArray::fromTokens(definition.arguments, ",", ""))
        if (argument.isNotEmpty())
            schema.emplace(argument.upToFirstOccurrenceOf(":", false, false).toStdString(),
                           argument.endsWith(":number") ? ArgumentKind::number : ArgumentKind::text);
    return schema;
}

// Catalogs are deliberately flat JSON objects of strings. Parse before using
// JUCE's object representation, which would otherwise discard duplicate keys.
class CatalogReader
{
public:
    explicit CatalogReader(const juce::String& json) : source(json.toStdString()) {}
    std::map<std::string, juce::String> read()
    {
        std::map<std::string, juce::String> values;
        expect('{');
        if (! take('}'))
        {
            do
            {
                const auto key = quoted().toStdString();
                expect(':');
                const auto value = quoted();
                if (! values.emplace(key, value).second) throw std::invalid_argument("duplicate key: " + key);
            } while (take(','));
            expect('}');
        }
        skip();
        if (position != source.size()) throw std::invalid_argument("trailing JSON content");
        return values;
    }
private:
    void skip() { while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position]))) ++position; }
    bool take(char ch) { skip(); if (position < source.size() && source[position] == ch) { ++position; return true; } return false; }
    void expect(char ch) { if (! take(ch)) throw std::invalid_argument("invalid catalog JSON"); }
    juce::String quoted()
    {
        skip();
        const auto start = position;
        expect('"');
        bool escaped = false;
        while (position < source.size())
        {
            const auto ch = source[position++];
            if (escaped && std::string("\"\\/bfnrtu").find(ch) == std::string::npos)
                throw std::invalid_argument("invalid JSON escape");
            if (ch == '"' && ! escaped)
            {
                juce::var value;
                const auto token = juce::String::fromUTF8(source.data() + start, static_cast<int>(position - start));
                if (juce::JSON::parse("[" + token + "]", value).failed() || ! value[0].isString()) throw std::invalid_argument("invalid JSON string");
                return value[0].toString();
            }
            if (static_cast<unsigned char>(ch) < 32) throw std::invalid_argument("control character in JSON string");
            escaped = ch == '\\' && ! escaped;
        }
        throw std::invalid_argument("unterminated JSON string");
    }
    std::string source;
    size_t position = 0;
};

struct Node
{
    enum class Kind { literal, argument, plural, select, pound };
    Kind kind = Kind::literal;
    std::string value;
    std::map<std::string, std::vector<Node>> choices;
};
using Template = std::vector<Node>;

class TemplateReader
{
public:
    TemplateReader(const juce::String& text, Schema declared) : source(text.toStdString()), schema(std::move(declared)) {}
    Template read()
    {
        if (source.empty() || source.size() > 16384) throw std::invalid_argument("empty or oversized template");
        auto nodes = sequence(false, {}, 0);
        if (position != source.size()) throw std::invalid_argument("unexpected closing brace");
        if (used.size() != schema.size()) throw std::invalid_argument("unused declared argument");
        return nodes;
    }
private:
    void skip() { while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position]))) ++position; }
    void expect(char ch) { skip(); if (position >= source.size() || source[position++] != ch) throw std::invalid_argument("invalid template syntax"); }
    std::string token()
    {
        skip();
        const auto start = position;
        while (position < source.size() && (std::isalnum(static_cast<unsigned char>(source[position])) || source[position] == '_' || source[position] == '=' || source[position] == '-')) ++position;
        if (start == position) throw std::invalid_argument("missing template token");
        return source.substr(start, position - start);
    }
    Template sequence(bool nested, const std::string& pluralArgument, int depth)
    {
        if (depth > 16) throw std::invalid_argument("template nesting limit");
        Template nodes;
        std::string literal;
        const auto flush = [&] { if (! literal.empty()) { nodes.push_back({ Node::Kind::literal, literal, {} }); literal.clear(); } };
        while (position < source.size())
        {
            const auto ch = source[position];
            if (ch == '}') break;
            ++position;
            if (ch == '\\')
            {
                if (position >= source.size() || std::string("{}#\\").find(source[position]) == std::string::npos)
                    throw std::invalid_argument("invalid template escape");
                literal += source[position++];
            }
            else if (ch == '#' && ! pluralArgument.empty()) { flush(); nodes.push_back({ Node::Kind::pound, pluralArgument, {} }); }
            else if (ch != '{') literal += ch;
            else
            {
                flush();
                auto name = token();
                if (! schema.contains(name)) throw std::invalid_argument("unknown argument: " + name);
                used.insert(name);
                skip();
                Node node { Node::Kind::argument, name, {} };
                if (position < source.size() && source[position] == ',')
                {
                    ++position;
                    const auto kind = token();
                    const bool plural = kind == "plural";
                    if ((! plural && kind != "select") || schema.at(name) != (plural ? ArgumentKind::number : ArgumentKind::text))
                        throw std::invalid_argument("argument type does not match plural/select");
                    node.kind = plural ? Node::Kind::plural : Node::Kind::select;
                    expect(',');
                    skip();
                    while (position < source.size() && source[position] != '}')
                    {
                        auto choice = token();
                        if (plural && choice != "zero" && choice != "one" && choice != "two" && choice != "few" && choice != "many" && choice != "other")
                        {
                            const auto numeric = juce::String(choice).substring(1);
                            if (choice.front() != '=' || numeric.isEmpty() || ! numeric.containsOnly("0123456789"))
                                throw std::invalid_argument("invalid plural category");
                        }
                        expect('{');
                        auto body = sequence(true, plural ? name : pluralArgument, depth + 1);
                        expect('}');
                        if (! node.choices.emplace(choice, std::move(body)).second) throw std::invalid_argument("duplicate variant");
                        skip();
                    }
                    if (! node.choices.contains("other")) throw std::invalid_argument("plural/select requires other");
                }
                expect('}');
                nodes.push_back(std::move(node));
            }
        }
        flush();
        if (nested && position == source.size()) throw std::invalid_argument("unterminated variant");
        return nodes;
    }
    std::string source;
    Schema schema;
    std::set<std::string> used;
    size_t position = 0;
};

using CompiledCatalog = std::map<MessageId, Template>;
CompiledCatalog compile(const Catalog& catalog, juce::StringArray& errors)
{
    CompiledCatalog compiled;
    try
    {
        const auto values = CatalogReader(catalog.json).read();
        std::set<std::string> known;
        for (const auto& definition : definitions)
        {
            known.insert(definition.key);
            const auto found = values.find(definition.key);
            if (found == values.end()) { errors.add(juce::String(definition.key) + ": missing key"); continue; }
            try { compiled.emplace(definition.id, TemplateReader(found->second, schemaFor(definition)).read()); }
            catch (const std::exception& error) { errors.add(juce::String(definition.key) + ": " + error.what()); }
        }
        for (const auto& [key, value] : values) if (! known.contains(key)) errors.add(juce::String(key) + ": unknown key");
    }
    catch (const std::exception& error) { errors.add(error.what()); }
    return compiled;
}
using Values = std::map<std::string, std::variant<juce::String, double>>;
bool parseNumber(const std::string& text, double& number)
{
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> number;
    return ! input.fail() && input.eof();
}
juce::String numberText(double number)
{
    // Classic-locale streams retain the macOS 12 baseline. Choose the lowest
    // significant precision that round-trips, retaining max_digits10 if the
    // library's reader cannot represent a finite subnormal candidate.
    std::string result;
    for (int precision = 1; precision <= std::numeric_limits<double>::max_digits10; ++precision)
    {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(precision) << number;
        result = output.str();
        double roundTrip = 0;
        if (parseNumber(result, roundTrip) && roundTrip == number && std::signbit(roundTrip) == std::signbit(number)) break;
    }
    return juce::String::fromUTF8(result.c_str());
}
juce::String render(const Template& nodes, const Values& arguments, PluralRule rule)
{
    juce::String result;
    for (const auto& node : nodes)
    {
        if (node.kind == Node::Kind::literal) { result += juce::String::fromUTF8(node.value.c_str()); continue; }
        const auto& value = arguments.at(node.value);
        if (node.kind == Node::Kind::argument || node.kind == Node::Kind::pound)
        {
            result += std::holds_alternative<double>(value) ? numberText(std::get<double>(value)) : std::get<juce::String>(value);
            continue;
        }
        std::string choice;
        if (node.kind == Node::Kind::plural)
        {
            const auto number = std::get<double>(value);
            choice = rule == PluralRule::oneOther && std::abs(number) == 1.0 ? "one" : "other";
            for (const auto& [variant, body] : node.choices)
            {
                if (variant.front() != '=') continue;
                double exact = 0;
                if (parseNumber(variant.substr(1), exact) && exact == number)
                {
                    choice = variant;
                    break;
                }
            }
        }
        else choice = std::get<juce::String>(value).toStdString();
        const auto selected = node.choices.find(choice);
        result += render(selected != node.choices.end() ? selected->second : node.choices.at("other"), arguments, rule);
    }
    return result;
}
}
juce::String normalizeLanguageTag(const juce::String& input)
{
    auto tag = input.trim().replaceCharacter('_', '-');
    auto parts = juce::StringArray::fromTokens(tag, "-", "");
    if (parts.isEmpty() || parts[0].length() < 2 || parts[0].length() > 8) return {};
    const auto alpha = [](const juce::String& value) { return value.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"); };
    const auto digits = [](const juce::String& value) { return value.containsOnly("0123456789"); };
    for (int i = 0; i < parts.size(); ++i)
    {
        const auto& part = parts[i];
        if (part.isEmpty() || part.length() > 8) return {};
        for (auto ch : part)
            if (ch > 127 || ! std::isalnum(static_cast<unsigned char>(ch))
                || (i == 0 && ! std::isalpha(static_cast<unsigned char>(ch)))) return {};
    }
    parts.set(0, parts[0].toLowerCase());
    int next = 1;
    // BCP 47 ordering: optional extended language, script, region, variants,
    // then extensions/private-use. Reject partial extensions such as zh-a.
    if (parts[0].length() <= 3)
        for (int count = 0; count < 3 && next < parts.size() && parts[next].length() == 3 && alpha(parts[next]); ++count, ++next)
            parts.set(next, parts[next].toLowerCase());
    if (next < parts.size() && parts[next].length() == 4 && alpha(parts[next]))
    {
        parts.set(next, parts[next].substring(0, 1).toUpperCase() + parts[next].substring(1).toLowerCase());
        ++next;
    }
    if (next < parts.size() && ((parts[next].length() == 2 && alpha(parts[next])) || (parts[next].length() == 3 && digits(parts[next]))))
    {
        parts.set(next, parts[next].toUpperCase());
        ++next;
    }
    std::set<juce::String> variants;
    while (next < parts.size() && (parts[next].length() >= 5 || (parts[next].length() == 4 && digits(parts[next].substring(0, 1)))))
    {
        parts.set(next, parts[next].toLowerCase());
        if (! variants.insert(parts[next++]).second) return {};
    }
    std::set<juce::String> extensions;
    while (next < parts.size())
    {
        const auto singleton = parts[next].toLowerCase();
        if (singleton.length() != 1 || ! extensions.insert(singleton).second) return {};
        parts.set(next++, singleton);
        const bool privateUse = singleton == "x";
        const auto start = next;
        while (next < parts.size() && (privateUse || parts[next].length() > 1))
        {
            parts.set(next, parts[next].toLowerCase());
            ++next;
        }
        if (next == start) return {};
    }
    return parts.joinIntoString("-");
}

Language resolveLanguage(const juce::StringArray& preferences, const std::vector<Language>& registry)
{
    const auto registered = [&registry](const juce::String& tag) -> const Language*
    {
        for (const auto& language : registry)
            if (normalizeLanguageTag(language.tag) == tag) return &language;
        for (const auto& language : registry)
            for (const auto& alias : language.aliases)
                if (normalizeLanguageTag(alias) == tag) return &language;
        return nullptr;
    };
    for (const auto& preference : preferences)
    {
        const auto normalized = normalizeLanguageTag(preference);
        if (normalized.isEmpty()) continue;
        if (const auto* language = registered(normalized)) return *language;
        auto parts = juce::StringArray::fromTokens(normalized, "-", "");
        int script = 1;
        if (parts[0].length() <= 3)
            for (int count = 0; count < 3 && script < parts.size() && parts[script].length() == 3
                    && parts[script].containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"); ++count) ++script;
        if (script < parts.size() && parts[script].length() == 4
                && parts[script].containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"))
            if (const auto* language = registered(parts[0] + "-" + parts[script])) return *language;
        if (const auto* language = registered(parts[0])) return *language;
    }
    for (const auto& language : registry) if (language.tag == "en") return language;
    throw std::invalid_argument("The translation registry requires an English fallback.");
}

std::vector<Catalog> productionCatalogs()
{
    const auto metadata = juce::JSON::parse(juce::String::fromUTF8(resources::languages_json, resources::languages_jsonSize));
    std::vector<Catalog> catalogs;
    if (auto* entries = metadata.getArray())
        for (const auto& entry : *entries)
        {
            Language language;
            language.tag = entry["tag"].toString();
            language.name = entry["name"].toString();
            language.resource = entry["resource"].toString();
            const auto rule = entry["pluralRule"].toString();
            if (rule != "oneOther" && rule != "other") throw std::invalid_argument("Unknown registered plural rule");
            language.pluralRule = rule == "oneOther" ? PluralRule::oneOther : PluralRule::other;
            if (auto* aliases = entry["aliases"].getArray()) for (const auto& alias : *aliases) language.aliases.add(alias.toString());
            int size = 0;
            const auto* data = resources::getNamedResource(language.resource.toRawUTF8(), size);
            catalogs.push_back({ language, data && juce::CharPointer_UTF8::isValidString(data, size) ? juce::String::fromUTF8(data, size) : juce::String() });
        }
    return catalogs;
}

juce::StringArray validateCatalog(const Catalog& catalog)
{
    juce::StringArray errors;
    compile(catalog, errors);
    return errors;
}
juce::StringArray validateProductionCatalogs()
{
    juce::StringArray errors;
    std::set<juce::String> tags;
    for (const auto& catalog : productionCatalogs())
    {
        if (normalizeLanguageTag(catalog.language.tag) != catalog.language.tag || catalog.language.name.isEmpty()
            || ! tags.insert(catalog.language.tag).second) errors.add("Invalid/duplicate registry metadata: " + catalog.language.tag);
        for (const auto& error : validateCatalog(catalog)) errors.add(catalog.language.tag + ": " + error);
    }
    if (! tags.contains("en")) errors.add("English fallback is missing");
    return errors;
}
struct Translation::Impl { Language selected; CompiledCatalog selectedMessages, englishMessages; PluralRule englishRule; };
Translation::Translation(const juce::StringArray& preferences, std::vector<Catalog> catalogs)
{
    std::vector<Language> registry;
    for (const auto& catalog : catalogs) registry.push_back(catalog.language);
    auto compiled = std::make_shared<Impl>();
    compiled->selected = resolveLanguage(preferences, registry);
    for (const auto& catalog : catalogs)
    {
        if (catalog.language.tag != "en" && catalog.language.tag != compiled->selected.tag) continue;
        juce::StringArray errors;
        auto messages = compile(catalog, errors);
        if (catalog.language.tag == "en")
        {
            if (! errors.isEmpty()) throw std::invalid_argument("Invalid English fallback: " + errors.joinIntoString("; ").toStdString());
            compiled->englishMessages = messages;
            compiled->englishRule = catalog.language.pluralRule;
        }
        if (catalog.language.tag == compiled->selected.tag) compiled->selectedMessages = std::move(messages);
    }
    impl = std::move(compiled);
}
const Language& Translation::language() const { return impl->selected; }
juce::String Translation::text(MessageId id, std::initializer_list<Argument> arguments) const
{
    const auto definition = std::find_if(std::begin(definitions), std::end(definitions), [id](const auto& item) { return item.id == id; });
    if (definition == std::end(definitions)) throw std::invalid_argument("Unknown message ID");
    const auto schema = schemaFor(*definition);
    Values values;
    for (const auto& argument : arguments)
    {
        const auto key = argument.name.toStdString();
        if (! schema.contains(key) || schema.at(key) != (std::holds_alternative<double>(argument.value) ? ArgumentKind::number : ArgumentKind::text)
            || ! values.emplace(key, argument.value).second
            || (std::holds_alternative<double>(argument.value) && ! std::isfinite(std::get<double>(argument.value))))
            throw std::invalid_argument("Invalid named argument: " + key);
    }
    if (values.size() != schema.size()) throw std::invalid_argument("Missing named argument");
    const auto found = impl->selectedMessages.find(id);
    return found != impl->selectedMessages.end() ? render(found->second, values, impl->selected.pluralRule)
                                                : render(impl->englishMessages.at(id), values, impl->englishRule);
}
}
