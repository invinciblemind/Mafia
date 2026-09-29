#pragma once

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

}  // namespace mafia
