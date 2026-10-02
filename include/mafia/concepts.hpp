#pragma once

#include <concepts>
#include <utility>

#include "mafia/action.hpp"
#include "mafia/game_view.hpp"
#include "mafia/player.hpp"
#include "mafia/role.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia {

// Класс годится в игроки: наследует Player и имеет все методы, нужные игре,
// с правильными типами результата. Забытый или неверно объявленный act()/vote()
// у новой роли будет пойман на этапе компиляции, а не в рантайме.
template <typename T>
concept PlayerLike = std::derived_from<T, Player> && requires(T& player, const GameView& view) {
    { player.act(view) } -> std::same_as<NightAction>;
    { player.vote(view) } -> std::same_as<PlayerId>;
    { player.act_async(view) } -> std::same_as<Task<NightAction>>;
    { player.vote_async(view) } -> std::same_as<Task<PlayerId>>;
    { player.role() } -> std::same_as<Role>;
    { player.is_alive() } -> std::convertible_to<bool>;
};

// Игрок, у которого роль известна при компиляции (static constexpr kRole).
template <typename T>
concept StaticRolePlayer = PlayerLike<T> && requires {
    { T::kRole } -> std::convertible_to<Role>;
};

template <typename T>
concept TownPlayer = StaticRolePlayer<T> && (team_of(T::kRole) == Team::Town);

template <typename T>
concept MafiaPlayer = StaticRolePlayer<T> && (team_of(T::kRole) == Team::Mafia);

// Роль с ночным действием: Доктор, Комиссар, Мафия, Маньяк (но не Мирный житель).
template <typename T>
concept NightActivePlayer = StaticRolePlayer<T> && acts_at_night(T::kRole);

// Ход ночных действий доступен только ролям, у которых оно есть: вызов для
// Civilian не скомпилируется. Мёртвые не ходят.
template <NightActivePlayer R>
NightAction take_night_turn(R& player, const GameView& view) {
    return player.is_alive() ? player.act(view) : NightAction{};
}

// Фабрика игроков: создать можно только тип, удовлетворяющий PlayerLike.
template <PlayerLike R, typename... Args>
SharedPtr<Player> make_player(Args&&... args) {
    return make_shared_ptr<R>(std::forward<Args>(args)...);
}

}  // namespace mafia
