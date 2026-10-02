#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mafia {

// Какие роли участвуют в игре. Мафия и Мирный житель есть всегда (число мафии
// задаётся формулой floor(N/k), остальные места занимают мирные); здесь —
// какие особые роли добавить сверх них. Каждая особая роль — не более одного
// игрока: все они однократные, а Вор занимает одно из мест мафии по формуле.
struct RoleConfig {
    int mafia_divisor = 3;  // k из формулы floor(N/k), k >= 3

    bool doctor = true;
    bool commissar = true;
    bool maniac = true;
    bool sergeant = false;
    bool resuscitator = false;
    bool thief = false;
};

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Набор по умолчанию — обязательные роли задания: Мафия, Мирный житель,
// Комиссар, Доктор, Маньяк.
RoleConfig default_role_config();

// Разбор YAML (поддерживаемое подмножество: комментарии "#", "ключ: значение",
// блочные списки "- элемент" и inline-списки "[a, b]"). Бросает ConfigError с
// номером строки при любой ошибке. Пример:
//
//   mafia_divisor: 3
//   roles:
//     - doctor
//     - commissar
//     - sergeant
//
// Если ключа roles нет — берётся набор по умолчанию; если он есть — включены
// ровно перечисленные роли (плюс всегда присутствующие мафия и мирные).
RoleConfig parse_role_config(std::string_view yaml_text);

RoleConfig load_role_config(const std::filesystem::path& file);

// Предупреждения о сомнительных, но допустимых наборах (например, Реаниматор,
// который по правилам вводится при двух и более стреляющих сторонах).
std::vector<std::string> role_config_warnings(const RoleConfig& config);

}  // namespace mafia
