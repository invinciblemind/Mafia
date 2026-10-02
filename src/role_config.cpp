#include "mafia/role_config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;

namespace mafia {

namespace {

struct Line {
    int number;
    int indent;
    std::string text;  // без комментария, без крайних пробелов
};

[[noreturn]] void fail(int line, const std::string& message) {
    throw ConfigError("строка " + std::to_string(line) + ": " + message);
}

std::string trim(const std::string& text) {
    const char* spaces = " \t\r";
    std::size_t first = text.find_first_not_of(spaces);
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(spaces) - first + 1);
}

// Комментарий начинается с '#' в начале строки или после пробела и вне кавычек.
std::string strip_comment(const std::string& text) {
    char quote = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quote != 0) {
            if (c == quote) {
                quote = 0;
            }
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '#' && (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\t')) {
            return text.substr(0, i);
        }
    }
    return text;
}

std::string unquote(const std::string& text) {
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front()) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::vector<Line> split_lines(std::string_view yaml) {
    std::vector<Line> lines;
    std::istringstream in{std::string(yaml)};
    std::string raw;
    for (int number = 1; std::getline(in, raw); ++number) {
        std::size_t indent = raw.find_first_not_of(' ');
        if (indent != std::string::npos && raw[indent] == '\t') {
            fail(number, "табуляция в отступе не поддерживается, используйте пробелы");
        }
        std::string text = trim(strip_comment(raw));
        if (text.empty()) {
            continue;
        }
        lines.push_back({number, static_cast<int>(indent), text});
    }
    return lines;
}

// Нижний регистр для ASCII и русских букв (UTF-8: А-П = D0 90..9F, Р-Я = D0 A0..AF).
std::string lowercase(const std::string& text) {
    std::string out = text;
    for (std::size_t i = 0; i < out.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(out[i]);
        if (c < 0x80) {
            out[i] = static_cast<char>(std::tolower(c));
        } else if (c == 0xD0 && i + 1 < out.size()) {
            unsigned char next = static_cast<unsigned char>(out[i + 1]);
            if (next >= 0x90 && next <= 0x9F) {
                out[i + 1] = static_cast<char>(next + 0x20);
            } else if (next >= 0xA0 && next <= 0xAF) {
                out[i] = static_cast<char>(0xD1);
                out[i + 1] = static_cast<char>(next - 0x20);
            }
            ++i;
        }
    }
    return out;
}

std::vector<std::string> parse_flow_list(const std::string& value, int line) {
    if (value.back() != ']') {
        fail(line, "inline-список должен заканчиваться на ']'");
    }
    std::string inner = value.substr(1, value.size() - 2);
    std::vector<std::string> items;
    std::istringstream in(inner);
    std::string item;
    while (std::getline(in, item, ',')) {
        std::string cleaned = unquote(trim(item));
        if (!cleaned.empty()) {
            items.push_back(cleaned);
        }
    }
    return items;
}

struct Entry {
    int line = 0;
    std::string scalar;
    std::vector<std::string> list;
    bool is_list = false;
};

std::map<std::string, Entry> parse_top_level(const std::vector<Line>& lines) {
    std::map<std::string, Entry> entries;
    std::size_t i = 0;
    while (i < lines.size()) {
        const Line& line = lines[i];
        if (line.indent != 0) {
            fail(line.number, "неожиданный отступ (вложенные структуры не поддерживаются)");
        }
        if (line.text.front() == '-') {
            fail(line.number, "элемент списка без ключа");
        }
        std::size_t colon = line.text.find(':');
        if (colon == std::string::npos) {
            fail(line.number, "ожидалось \"ключ: значение\"");
        }
        std::string key = trim(line.text.substr(0, colon));
        std::string value = trim(line.text.substr(colon + 1));
        if (key.empty()) {
            fail(line.number, "пустой ключ");
        }
        if (entries.count(key) != 0) {
            fail(line.number, "ключ \"" + key + "\" указан дважды");
        }

        Entry entry;
        entry.line = line.number;
        ++i;
        if (value.empty()) {
            // Значение — блочный список на следующих строках ("- элемент").
            while (i < lines.size() && lines[i].text.front() == '-') {
                std::string item = unquote(trim(lines[i].text.substr(1)));
                if (item.empty()) {
                    fail(lines[i].number, "пустой элемент списка");
                }
                entry.list.push_back(item);
                entry.is_list = true;
                ++i;
            }
        } else if (value.front() == '[') {
            entry.list = parse_flow_list(value, line.number);
            entry.is_list = true;
        } else {
            entry.scalar = unquote(value);
        }
        entries[key] = std::move(entry);
    }
    return entries;
}

void enable_role(RoleConfig& config, const std::string& raw_name, int line) {
    static const std::map<std::string, bool RoleConfig::*> toggles = {
        {"doctor", &RoleConfig::doctor},         {"доктор", &RoleConfig::doctor},
        {"commissar", &RoleConfig::commissar},   {"комиссар", &RoleConfig::commissar},
        {"maniac", &RoleConfig::maniac},         {"маньяк", &RoleConfig::maniac},
        {"sergeant", &RoleConfig::sergeant},     {"сержант", &RoleConfig::sergeant},
        {"resuscitator", &RoleConfig::resuscitator}, {"реаниматор", &RoleConfig::resuscitator},
        {"thief", &RoleConfig::thief},           {"вор", &RoleConfig::thief},
    };
    static const std::vector<std::string> always_present = {"mafia", "мафия", "civilian", "мирный", "мирный житель"};

    std::string name = lowercase(raw_name);
    if (std::ranges::find(always_present, name) != always_present.end()) {
        return;  // мафия и мирные есть в любой игре, перечислить их можно, но не обязательно
    }
    auto it = toggles.find(name);
    if (it == toggles.end()) {
        fail(line, "неизвестная роль \"" + raw_name +
                       "\" (допустимо: doctor, commissar, maniac, sergeant, resuscitator, thief, mafia, civilian)");
    }
    bool& flag = config.*(it->second);
    if (flag) {
        fail(line, "роль \"" + raw_name + "\" указана дважды");
    }
    flag = true;
}

}  // namespace

RoleConfig default_role_config() { return RoleConfig{}; }

RoleConfig parse_role_config(std::string_view yaml_text) {
    std::map<std::string, Entry> entries = parse_top_level(split_lines(yaml_text));

    RoleConfig config;
    for (const auto& [key, entry] : entries) {
        if (key != "mafia_divisor" && key != "roles") {
            fail(entry.line, "неизвестный ключ \"" + key + "\" (допустимо: mafia_divisor, roles)");
        }
    }

    if (auto it = entries.find("mafia_divisor"); it != entries.end()) {
        const Entry& entry = it->second;
        int value = 0;
        auto [end, error] = std::from_chars(entry.scalar.data(), entry.scalar.data() + entry.scalar.size(), value);
        if (entry.is_list || entry.scalar.empty() || error != std::errc{} ||
            end != entry.scalar.data() + entry.scalar.size()) {
            fail(entry.line, "mafia_divisor должен быть целым числом");
        }
        if (value < 3) {
            fail(entry.line, "mafia_divisor должен быть не меньше 3 (получено " + std::to_string(value) + ")");
        }
        config.mafia_divisor = value;
    }

    if (auto it = entries.find("roles"); it != entries.end()) {
        const Entry& entry = it->second;
        if (!entry.is_list && !entry.scalar.empty()) {
            fail(entry.line, "roles должен быть списком ролей");
        }
        // Явный список: включены только перечисленные особые роли.
        config.doctor = config.commissar = config.maniac = false;
        config.sergeant = config.resuscitator = config.thief = false;
        for (const std::string& name : entry.list) {
            enable_role(config, name, entry.line);
        }
    }
    return config;
}

RoleConfig load_role_config(const fs::path& file) {
    std::ifstream in(file);
    if (!in) {
        throw ConfigError("не удалось открыть файл конфигурации: " + file.string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    try {
        return parse_role_config(text.str());
    } catch (const ConfigError& error) {
        throw ConfigError(file.string() + ": " + error.what());
    }
}

std::vector<std::string> role_config_warnings(const RoleConfig& config) {
    std::vector<std::string> warnings;
    // Мафия стреляет всегда; нужна ещё одна стреляющая сторона.
    if (config.resuscitator && !config.maniac && !config.commissar) {
        warnings.push_back(
            "Реаниматор по правилам вводится при двух и более стреляющих сторонах, а кроме мафии стреляющих "
            "сторон (Маньяк, Комиссар) в этой конфигурации нет.");
    }
    if (config.sergeant && !config.commissar) {
        warnings.push_back("Сержант без Комиссара бесполезен: ему некого знать и некого заменять.");
    }
    return warnings;
}

}  // namespace mafia
