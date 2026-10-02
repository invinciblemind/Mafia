#pragma once

#include "mafia/player.hpp"

namespace mafia::roles {

// Маньяк — одиночка, играет сам за себя. Каждую ночь убивает случайного
// живого игрока (мафиози или мирного — ему всё равно), причём, в отличие
// от Доктора, у Маньяка нет правила "не дважды подряд одного и того же".
class Maniac : public Player {
public:
    using Player::Player;

    static constexpr Role kRole = Role::Maniac;
    Role role() const noexcept override { return kRole; }

    PlayerId vote(const GameView& view) override;
    NightAction act(const GameView& view) override;
};

}  // namespace mafia::roles
