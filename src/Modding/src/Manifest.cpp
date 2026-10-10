#include "OpenLoco/Modding/Manifest.hpp"

#include <cctype>
#include <map>
#include <utility>

namespace OpenLoco::Modding
{
    namespace
    {
        struct JsonValue
        {
            enum class Kind
            {
                string,
                number,
                array,
            };

            Kind kind = Kind::string;
            std::string text;
            uint32_t number{};
            std::vector<std::string> values;
        };

        class Parser
        {
        private:
            std::string_view _text;
            size_t _index = 0;
            std::string _error;

        public:
            explicit Parser(std::string_view text)
                : _text{ text }
            {
            }

            [[nodiscard]] const std::string& error() const
            {
                return _error;
            }

            [[nodiscard]] bool ok() const
            {
                return _error.empty();
            }

            bool fail(std::string message)
            {
                if (_error.empty())
                {
                    _error = "manifest JSON at byte " + std::to_string(_index) + ": " + std::move(message);
                }
                return false;
            }

            void skipWhitespace()
            {
                while (_index < _text.size())
                {
                    const char ch = _text[_index];
                    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
                    {
                        break;
                    }
                    ++_index;
                }
            }

            [[nodiscard]] char peek() const
            {
                return _index < _text.size() ? _text[_index] : '\0';
            }

            char get()
            {
                return _index < _text.size() ? _text[_index++] : '\0';
            }

            bool expect(char expected)
            {
                skipWhitespace();
                if (peek() != expected)
                {
                    return fail(std::string("expected '") + expected + "'");
                }
                get();
                return true;
            }

            bool parseString(std::string& out)
            {
                skipWhitespace();
                if (!expect('"'))
                {
                    return false;
                }
                out.clear();
                while (_index < _text.size())
                {
                    const unsigned char ch = static_cast<unsigned char>(get());
                    if (ch == '"')
                    {
                        return true;
                    }
                    if (ch == '\\')
                    {
                        if (!parseEscape(out))
                        {
                            return false;
                        }
                        continue;
                    }
                    if (ch < 0x20)
                    {
                        return fail("unescaped control character in string");
                    }
                    out.push_back(static_cast<char>(ch));
                }
                return fail("unterminated string");
            }

            bool parseNumber(uint32_t& out)
            {
                skipWhitespace();
                if (!std::isdigit(static_cast<unsigned char>(peek())))
                {
                    return fail("expected an unsigned integer");
                }
                if (peek() == '0' && _index + 1 < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_index + 1])))
                {
                    return fail("leading zeroes are not allowed");
                }
                uint64_t value = 0;
                while (std::isdigit(static_cast<unsigned char>(peek())))
                {
                    value = value * 10u + static_cast<uint64_t>(get() - '0');
                    if (value > 0xFFFFFFFFull)
                    {
                        return fail("integer is out of range");
                    }
                }
                out = static_cast<uint32_t>(value);
                return true;
            }

            bool parseArray(std::vector<std::string>& out)
            {
                if (!expect('['))
                {
                    return false;
                }
                out.clear();
                skipWhitespace();
                if (peek() == ']')
                {
                    get();
                    return true;
                }
                while (ok())
                {
                    std::string item;
                    if (!parseString(item))
                    {
                        return false;
                    }
                    out.push_back(std::move(item));
                    skipWhitespace();
                    if (peek() == ']')
                    {
                        get();
                        return true;
                    }
                    if (!expect(','))
                    {
                        return false;
                    }
                }
                return false;
            }

            bool parseValue(JsonValue& out)
            {
                skipWhitespace();
                const char ch = peek();
                if (ch == '"')
                {
                    out.kind = JsonValue::Kind::string;
                    return parseString(out.text);
                }
                if (ch == '[')
                {
                    out.kind = JsonValue::Kind::array;
                    return parseArray(out.values);
                }
                if (std::isdigit(static_cast<unsigned char>(ch)))
                {
                    out.kind = JsonValue::Kind::number;
                    return parseNumber(out.number);
                }
                return fail("unsupported value; manifest fields must be strings, string arrays, or unsigned integers");
            }

            bool atEnd()
            {
                skipWhitespace();
                if (peek() != '\0')
                {
                    return fail("extra data after the manifest object");
                }
                return true;
            }

            bool parseObject(std::map<std::string, JsonValue>& out)
            {
                if (!expect('{'))
                {
                    return false;
                }
                skipWhitespace();
                if (peek() == '}')
                {
                    get();
                    return true;
                }
                while (ok())
                {
                    std::string key;
                    if (!parseString(key))
                    {
                        return false;
                    }
                    if (out.find(key) != out.end())
                    {
                        return fail("duplicate key \"" + key + "\"");
                    }
                    if (!expect(':'))
                    {
                        return false;
                    }
                    JsonValue value;
                    if (!parseValue(value))
                    {
                        return false;
                    }
                    out.emplace(std::move(key), std::move(value));
                    skipWhitespace();
                    if (peek() == '}')
                    {
                        get();
                        return true;
                    }
                    if (!expect(','))
                    {
                        return false;
                    }
                }
                return false;
            }

        private:
            bool parseEscape(std::string& out)
            {
                if (_index >= _text.size())
                {
                    return fail("unterminated escape");
                }
                const char escaped = get();
                switch (escaped)
                {
                    case '"':
                    case '\\':
                    case '/':
                        out.push_back(escaped);
                        return true;
                    case 'b':
                        out.push_back('\b');
                        return true;
                    case 'f':
                        out.push_back('\f');
                        return true;
                    case 'n':
                        out.push_back('\n');
                        return true;
                    case 'r':
                        out.push_back('\r');
                        return true;
                    case 't':
                        out.push_back('\t');
                        return true;
                    case 'u':
                        return parseUnicodeEscape(out);
                    default:
                        return fail("unsupported string escape");
                }
            }

            bool parseUnicodeEscape(std::string& out)
            {
                uint32_t code = 0;
                for (int i = 0; i < 4; ++i)
                {
                    if (_index >= _text.size())
                    {
                        return fail("short unicode escape");
                    }
                    const unsigned char hex = static_cast<unsigned char>(get());
                    code <<= 4u;
                    if (hex >= '0' && hex <= '9')
                    {
                        code += hex - '0';
                    }
                    else if (hex >= 'a' && hex <= 'f')
                    {
                        code += hex - 'a' + 10;
                    }
                    else if (hex >= 'A' && hex <= 'F')
                    {
                        code += hex - 'A' + 10;
                    }
                    else
                    {
                        return fail("invalid unicode escape");
                    }
                }
                if (code >= 0xD800 && code <= 0xDFFF)
                {
                    return fail("unicode surrogate escapes are not supported");
                }
                appendUtf8(out, code);
                return true;
            }

            static void appendUtf8(std::string& out, uint32_t code)
            {
                if (code <= 0x7F)
                {
                    out.push_back(static_cast<char>(code));
                }
                else if (code <= 0x7FF)
                {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
            }
        };

        bool isSegment(std::string_view text)
        {
            if (text.empty() || text.front() < 'a' || text.front() > 'z')
            {
                return false;
            }
            if (text.back() == '-')
            {
                return false;
            }
            for (const char ch : text)
            {
                const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
                if (!ok)
                {
                    return false;
                }
            }
            return true;
        }

        bool isModId(std::string_view id)
        {
            const auto dot = id.find('.');
            if (dot == std::string_view::npos || dot == 0 || dot + 1 >= id.size())
            {
                return false;
            }
            size_t start = 0;
            while (start < id.size())
            {
                const auto next = id.find('.', start);
                const auto segment = id.substr(start, next == std::string_view::npos ? std::string_view::npos : next - start);
                if (!isSegment(segment))
                {
                    return false;
                }
                if (next == std::string_view::npos)
                {
                    return true;
                }
                start = next + 1;
            }
            return false;
        }

        bool takeString(std::map<std::string, JsonValue>& fields, const char* key, bool required, std::string& out, std::string& error)
        {
            const auto it = fields.find(key);
            if (it == fields.end())
            {
                if (!required)
                {
                    out.clear();
                    return true;
                }
                error = std::string("missing required field \"") + key + "\"";
                return false;
            }
            if (it->second.kind != JsonValue::Kind::string)
            {
                error = std::string("field \"") + key + "\" must be a string";
                return false;
            }
            out = std::move(it->second.text);
            fields.erase(it);
            return true;
        }

        bool takeArray(std::map<std::string, JsonValue>& fields, const char* key, std::vector<std::string>& out, std::string& error)
        {
            const auto it = fields.find(key);
            if (it == fields.end())
            {
                out.clear();
                return true;
            }
            if (it->second.kind != JsonValue::Kind::array)
            {
                error = std::string("field \"") + key + "\" must be an array of strings";
                return false;
            }
            out = std::move(it->second.values);
            fields.erase(it);
            return true;
        }

        bool uniqueIds(const std::vector<std::string>& ids, const char* field, std::string& error)
        {
            for (size_t i = 0; i < ids.size(); ++i)
            {
                if (!isModId(ids[i]))
                {
                    error = std::string(field) + " contains an invalid mod id \"" + ids[i] + "\"";
                    return false;
                }
                for (size_t j = 0; j < i; ++j)
                {
                    if (ids[j] == ids[i])
                    {
                        error = std::string(field) + " lists \"" + ids[i] + "\" more than once";
                        return false;
                    }
                }
            }
            return true;
        }
    }

    std::optional<Version> parseVersion(std::string_view text)
    {
        Version version;
        uint32_t* parts[3] = { &version.major, &version.minor, &version.patch };
        size_t part = 0;
        if (text.empty() || !std::isdigit(static_cast<unsigned char>(text.front())))
        {
            return std::nullopt;
        }
        size_t index = 0;
        while (part < 3)
        {
            if (index >= text.size() || !std::isdigit(static_cast<unsigned char>(text[index])))
            {
                return std::nullopt;
            }
            if (text[index] == '0' && index + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[index + 1])))
            {
                return std::nullopt;
            }
            uint32_t value = 0;
            while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])))
            {
                const uint32_t digit = static_cast<uint32_t>(text[index] - '0');
                if (value > (0xFFFFFFFFu - digit) / 10u)
                {
                    return std::nullopt;
                }
                value = value * 10u + digit;
                ++index;
            }
            *parts[part] = value;
            ++part;
            if (part == 3)
            {
                break;
            }
            if (index >= text.size() || text[index] != '.')
            {
                return std::nullopt;
            }
            ++index;
        }
        if (index != text.size())
        {
            return std::nullopt;
        }
        return version;
    }

    bool isApiCompatible(Version running, Version required)
    {
        return running.major == required.major && running.minor == required.minor && running.patch >= required.patch;
    }

    std::optional<ParseFailure> parseManifest(std::string_view json, Manifest& out)
    {
        if (!json.empty() && static_cast<unsigned char>(json.front()) == 0xEF)
        {
            if (json.size() < 3 || static_cast<unsigned char>(json[1]) != 0xBB || static_cast<unsigned char>(json[2]) != 0xBF)
            {
                return ParseFailure{ "manifest JSON is not valid UTF-8" };
            }
            json.remove_prefix(3);
        }

        Parser parser(json);
        std::map<std::string, JsonValue> fields;
        if (!parser.parseObject(fields) || !parser.atEnd())
        {
            return ParseFailure{ parser.error() };
        }

        Manifest manifest;
        std::string error;
        if (!takeString(fields, "id", true, manifest.id, error)
            || !takeString(fields, "name", true, manifest.name, error)
            || !takeString(fields, "author", false, manifest.author, error)
            || !takeString(fields, "version", true, manifest.version, error)
            || !takeString(fields, "engineApiVersion", true, manifest.engineApiVersion, error)
            || !takeArray(fields, "dependencies", manifest.dependencies, error)
            || !takeArray(fields, "optionalDependencies", manifest.optionalDependencies, error)
            || !takeArray(fields, "conflicts", manifest.conflicts, error)
            || !takeArray(fields, "capabilities", manifest.capabilities, error)
            || !takeArray(fields, "platforms", manifest.platforms, error)
            || !takeString(fields, "description", false, manifest.description, error)
            || !takeString(fields, "license", false, manifest.license, error)
            || !takeArray(fields, "entryPoints", manifest.entryPoints, error))
        {
            return ParseFailure{ error };
        }

        const auto schema = fields.find("stateSchemaVersion");
        if (schema == fields.end())
        {
            return ParseFailure{ "missing required field \"stateSchemaVersion\"" };
        }
        if (schema->second.kind != JsonValue::Kind::number)
        {
            return ParseFailure{ "field \"stateSchemaVersion\" must be an unsigned integer" };
        }
        manifest.stateSchemaVersion = schema->second.number;
        fields.erase(schema);

        if (!isModId(manifest.id))
        {
            return ParseFailure{ "field \"id\" must be a dotted lowercase id such as \"openloco.sample.hello\"" };
        }
        if (manifest.name.empty())
        {
            return ParseFailure{ "field \"name\" must not be empty" };
        }
        if (!parseVersion(manifest.version))
        {
            return ParseFailure{ "field \"version\" must be MAJOR.MINOR.PATCH" };
        }
        if (!parseVersion(manifest.engineApiVersion))
        {
            return ParseFailure{ "field \"engineApiVersion\" must be MAJOR.MINOR.PATCH" };
        }
        if (manifest.stateSchemaVersion == 0)
        {
            return ParseFailure{ "field \"stateSchemaVersion\" must be at least 1" };
        }
        if (!uniqueIds(manifest.dependencies, "dependencies", error)
            || !uniqueIds(manifest.optionalDependencies, "optionalDependencies", error)
            || !uniqueIds(manifest.conflicts, "conflicts", error))
        {
            return ParseFailure{ error };
        }
        for (const auto& dependency : manifest.dependencies)
        {
            if (dependency == manifest.id)
            {
                return ParseFailure{ "a mod cannot depend on itself" };
            }
        }
        for (const auto& dependency : manifest.optionalDependencies)
        {
            if (dependency == manifest.id)
            {
                return ParseFailure{ "a mod cannot optionally depend on itself" };
            }
        }
        for (const auto& conflict : manifest.conflicts)
        {
            if (conflict == manifest.id)
            {
                return ParseFailure{ "a mod cannot conflict with itself" };
            }
        }
        for (const auto& platform : manifest.platforms)
        {
            if (platform.empty())
            {
                return ParseFailure{ "platforms contains an empty name" };
            }
            for (const char ch : platform)
            {
                if (ch < 'a' || ch > 'z')
                {
                    return ParseFailure{ "platform \"" + platform + "\" must be lowercase letters, such as \"windows\"" };
                }
            }
        }
        for (const auto& capability : manifest.capabilities)
        {
            if (capability.empty())
            {
                return ParseFailure{ "capabilities contains an empty name" };
            }
        }
        for (const auto& entryPoint : manifest.entryPoints)
        {
            if (entryPoint.empty())
            {
                return ParseFailure{ "entryPoints contains an empty name" };
            }
        }

        out = std::move(manifest);
        return std::nullopt;
    }
}
