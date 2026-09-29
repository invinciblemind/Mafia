#pragma once

#include "mafia/player.hpp"

namespace mafia::roles {

// Мирный житель — роль без специальных возможностей. Днём голосует наравне
// со всеми игроками, ночью никаких действий не совершает.
class Civilian : public Player {
public:
    using Player::Player;  // наследуем конструктор Player(id, name)

    Role role() const noexcept override { return Role::Civilian; }

    PlayerId vote(const GameView& view) override;
    NightAction act(const GameView& view) override;
};

}  // namespace mafia::roles
