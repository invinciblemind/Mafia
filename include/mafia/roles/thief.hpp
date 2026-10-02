#pragma once

#include "mafia/roles/mafia.hpp"

namespace mafia::roles {

// Вор — мафиози, который ночью, вместо участия в выборе жертвы, блокирует
// спецвозможности мирного игрока: если цель — Доктор, Комиссар или Реаниматор,
// его ночное действие в эту ночь не срабатывает (на Мафию, Маньяка и
// Мирного жителя блокировка не действует — нечего блокировать).
//
// Днём ведёт себя как остальная банда (наследует голосование Mafia). Если в
// банде не осталось никого, кроме воров, Вор занимает место Босса (см.
// MafiaCouncil::current_boss) и стреляет, как обычный мафиози.
class Thief : public Mafia {
public:
    using Mafia::Mafia;

    static constexpr Role kRole = Role::Thief;
    Role role() const noexcept override { return kRole; }

    NightAction act(const GameView& view) override;
    Task<NightAction> act_async(const GameView& view) override;
};

}  // namespace mafia::roles
