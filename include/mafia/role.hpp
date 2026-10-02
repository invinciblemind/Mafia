#pragma once

#include <string_view>

namespace mafia {

// Роль игрока — какую фигуру он отыгрывает в игре.
enum class Role {
    Civilian,   // Мирный житель
    Mafia,      // Мафия
    Commissar,  // Комиссар
    Doctor,     // Доктор
    Maniac,     // Маньяк
};

// Команда, за победу которой играет роль. Используется для проверки условий
// завершения игры (например: "убиты все мафиози и Маньяк" -> победа Town).
enum class Team {
    Town,         // мирные жители и их помощники (Доктор, Комиссар)
    Mafia,        // клан мафии
    Independent,  // одиночки со своим условием победы (Маньяк)
};

// Команда однозначно определяется ролью — это чистая функция, а не
// виртуальный метод, поэтому она constexpr и не требует объекта игрока.
constexpr Team team_of(Role role) noexcept {
    switch (role) {
        case Role::Mafia:
            return Team::Mafia;
        case Role::Maniac:
            return Team::Independent;
        case Role::Civilian:
        case Role::Commissar:
        case Role::Doctor:
            return Team::Town;
    }
    return Team::Town;
}

// Есть ли у роли ночное действие. Нужна концепту NightActivePlayer, поэтому
// constexpr: решение принимается при компиляции по Role, а не по объекту.
constexpr bool acts_at_night(Role role) noexcept {
    return role != Role::Civilian;
}

// Название роли для вывода игроку и для файловых логов.
constexpr std::string_view role_name(Role role) noexcept {
    switch (role) {
        case Role::Civilian:
            return "Мирный житель";
        case Role::Mafia:
            return "Мафия";
        case Role::Commissar:
            return "Комиссар";
        case Role::Doctor:
            return "Доктор";
        case Role::Maniac:
            return "Маньяк";
    }
    return "?";
}

}  // namespace mafia
