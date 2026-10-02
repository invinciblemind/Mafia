#pragma once

#include <vector>

#include "mafia/player.hpp"

namespace mafia::roles {

// Реаниматор — помощник Комиссара. Ходит ПОСЛЕДНИМ, на втором этапе ночи
// (acts_after_resolution): когда остальные ночные действия уже разрешены и
// известно, кто погибнет, ему сообщают имена погибших этой ночью (без ролей,
// GameView::killed_tonight), и он может воскресить любого из них. Погибших на
// дневном голосовании и в прежние ночи воскрешать нельзя. Одного и того же
// игрока дважды воскрешать нельзя. Ход необязателен.
class Resuscitator : public Player {
public:
    using Player::Player;

    static constexpr Role kRole = Role::Resuscitator;
    Role role() const noexcept override { return kRole; }

    bool acts_after_resolution() const noexcept override { return true; }

    PlayerId vote(const GameView& view) override;
    NightAction act(const GameView& view) override;
    Task<PlayerId> vote_async(const GameView& view) override;
    Task<NightAction> act_async(const GameView& view) override;

    // Вызывается Ведущим, когда воскрешение действительно произошло (если ход
    // заблокирован Вором, право на этого игрока не тратится).
    void note_revived(PlayerId id) { revived_.push_back(id); }

private:
    std::vector<PlayerId> revivable(const GameView& view) const;

    std::vector<PlayerId> revived_;
};

}  // namespace mafia::roles
