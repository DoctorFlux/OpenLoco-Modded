#include "OpenLoco/Modding/Activation.hpp"

#include <cctype>
#include <fstream>
#include <map>
#include <set>

namespace OpenLoco::Modding
{
    namespace
    {
        constexpr std::uintmax_t kMaxActionBytes = 1024 * 1024;

        struct JsonValue
        {
            enum class Kind
            {
                array,
            };
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

            bool fail(std::string message)
            {
                if (_error.empty())
                {
                    _error = std::move(message);
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
                        if (_index >= _text.size())
                        {
                            return fail("unterminated escape");
                        }
                        const char escaped = get();
                        if (escaped == '"' || escaped == '\\' || escaped == '/')
                        {
                            out.push_back(escaped);
                            continue;
                        }
                        if (escaped == 'n')
                        {
                            out.push_back('\n');
                            continue;
                        }
                        return fail("unsupported string escape in an action file");
                    }
                    if (ch < 0x20)
                    {
                        return fail("unescaped control character in string");
                    }
                    out.push_back(static_cast<char>(ch));
                }
                return fail("unterminated string");
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
                while (_error.empty())
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
                    skipWhitespace();
                    return peek() == '\0' || fail("extra data after object");
                }
                while (_error.empty())
                {
                    std::string key;
                    if (!parseString(key) || !expect(':'))
                    {
                        return false;
                    }
                    if (out.find(key) != out.end())
                    {
                        return fail("duplicate key \"" + key + "\"");
                    }
                    JsonValue value;
                    if (!parseArray(value.values))
                    {
                        return false;
                    }
                    out.emplace(std::move(key), std::move(value));
                    skipWhitespace();
                    if (peek() == '}')
                    {
                        get();
                        skipWhitespace();
                        return peek() == '\0' || fail("extra data after object");
                    }
                    if (!expect(','))
                    {
                        return false;
                    }
                }
                return false;
            }
        };

        std::optional<std::string> readText(const std::filesystem::path& path, std::string& error)
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (ec)
            {
                error = "could not read " + path.string();
                return std::nullopt;
            }
            if (size > kMaxActionBytes)
            {
                error = path.string() + " is larger than 1 MiB";
                return std::nullopt;
            }
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                error = "could not read " + path.string();
                return std::nullopt;
            }
            std::string text(static_cast<size_t>(size), '\0');
            stream.read(text.data(), static_cast<std::streamsize>(size));
            if (!stream)
            {
                error = "could not read " + path.string();
                return std::nullopt;
            }
            return text;
        }

        bool parseStringArrayFile(std::string_view json, const std::string& requiredKey, std::vector<std::string>& out, std::string& error)
        {
            Parser parser(json);
            std::map<std::string, JsonValue> fields;
            if (!parser.parseObject(fields))
            {
                error = parser.error();
                return false;
            }
            if (fields.size() != 1 || fields.begin()->first != requiredKey)
            {
                error = "action file must contain only \"" + requiredKey + "\"";
                return false;
            }
            out = std::move(fields.begin()->second.values);
            return true;
        }

        bool isInsideRoot(const std::filesystem::path& root, const std::filesystem::path& relative, std::filesystem::path& full, std::string& error)
        {
            if (relative.empty() || relative.is_absolute())
            {
                error = "entry point must be a relative path";
                return false;
            }
            for (const auto& part : relative)
            {
                if (part == "..")
                {
                    error = "entry point cannot contain '..'";
                    return false;
                }
            }
            const auto rootNormal = root.lexically_normal();
            full = (rootNormal / relative).lexically_normal();
            const auto rootText = rootNormal.generic_string();
            const auto fullText = full.generic_string();
            if (fullText != rootText && fullText.rfind(rootText + "/", 0) != 0)
            {
                error = "entry point escapes the mod directory";
                return false;
            }
            return true;
        }

        void report(const HostCallbacks& callbacks, bool isError, std::string_view message)
        {
            if (isError)
            {
                if (callbacks.onError)
                {
                    callbacks.onError(message);
                }
            }
            else if (callbacks.onInfo)
            {
                callbacks.onInfo(message);
            }
        }

        const DiscoveredMod* findMod(const LoadResult& loaded, const std::string& id)
        {
            for (const auto& mod : loaded.mods)
            {
                if (mod.manifest.id == id)
                {
                    return &mod;
                }
            }
            return nullptr;
        }
    }

    ActivationResult activateMods(const std::filesystem::path& directory, const HostCallbacks& callbacks)
    {
        ActivationResult result;
        const auto loaded = loadModDirectory(directory);
        result.errors = loaded.errors;
        for (const auto& error : loaded.errors)
        {
            report(callbacks, true, error);
        }
        if (!loaded.errors.empty())
        {
            return result;
        }

        std::set<std::string> enabled;
        const auto enabledPath = directory / "enabled.json";
        if (std::filesystem::is_regular_file(enabledPath))
        {
            std::string error;
            const auto text = readText(enabledPath, error);
            std::vector<std::string> ids;
            if (!text || !parseStringArrayFile(*text, "enabled", ids, error))
            {
                result.errors.push_back(enabledPath.string() + ": " + error);
                report(callbacks, true, result.errors.back());
                return result;
            }
            for (const auto& id : ids)
            {
                if (!enabled.insert(id).second)
                {
                    result.errors.push_back("enabled.json lists \"" + id + "\" more than once");
                    report(callbacks, true, result.errors.back());
                    return result;
                }
            }
        }

        for (const auto& id : loaded.loadOrder)
        {
            const auto* mod = findMod(loaded, id);
            ActivationRecord record;
            record.id = id;
            if (mod == nullptr)
            {
                record.state = ModLifecycle::failed;
                record.error = "resolved mod \"" + id + "\" was not discovered";
                result.errors.push_back(record.error);
                report(callbacks, true, record.error);
                result.records.push_back(std::move(record));
                continue;
            }
            if (enabled.find(id) == enabled.end())
            {
                record.state = ModLifecycle::discovered;
                record.messages.push_back(id + " is installed and not enabled");
                report(callbacks, false, record.messages.back());
                result.records.push_back(std::move(record));
                continue;
            }

            bool failed = false;
            if (mod->manifest.entryPoints.empty())
            {
                record.messages.push_back(id + " enabled with no entry point");
                report(callbacks, false, record.messages.back());
            }
            for (const auto& entry : mod->manifest.entryPoints)
            {
                std::string error;
                std::filesystem::path full;
                if (!isInsideRoot(mod->root, std::filesystem::path(entry), full, error))
                {
                    failed = true;
                    record.error = id + ": " + error;
                    break;
                }
                const auto text = readText(full, error);
                std::vector<std::string> lines;
                if (!text || !parseStringArrayFile(*text, "log", lines, error))
                {
                    failed = true;
                    record.error = full.string() + ": " + error;
                    break;
                }
                for (const auto& line : lines)
                {
                    const auto message = id + ": " + line;
                    record.messages.push_back(message);
                    report(callbacks, false, message);
                }
            }

            if (failed)
            {
                record.state = ModLifecycle::failed;
                result.errors.push_back(record.error);
                report(callbacks, true, record.error);
            }
            else
            {
                record.state = ModLifecycle::active;
            }
            result.records.push_back(std::move(record));
        }

        for (const auto& id : enabled)
        {
            if (findMod(loaded, id) == nullptr)
            {
                const auto message = "enabled.json requests missing mod \"" + id + "\"";
                result.errors.push_back(message);
                report(callbacks, true, message);
            }
        }
        return result;
    }
}
