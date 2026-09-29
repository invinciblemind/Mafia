#pragma once

#include <vector>

#include "mafia/action.hpp"
#include "mafia/game_view.hpp"

namespace mafia::roles::detail {

// Общая логика "выбрать случайного живого игрока, кроме нескольких id" —
// нужна почти каждой роли и для дневного голосования, и для ночного хода.
// Пока роли решают случайным ботом; когда появятся интерактивный режим и
// ИИ-агент, эта функция останется резервной стратегией по умолчанию (fallback).
//
// Возвращает kNoTarget, если подходящих кандидатов не осталось.
PlayerId pick_random_target(const GameView& view, const std::vector<PlayerId>& exclude);

}  // namespace mafia::roles::detail
