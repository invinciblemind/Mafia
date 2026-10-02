#pragma once

#include <string>
#include <vector>

#include "mafia/action.hpp"
#include "mafia/coroutines.hpp"
#include "mafia/game_view.hpp"
#include "mafia/player.hpp"

namespace mafia::roles::detail {

// Общая логика "выбрать случайного живого игрока, кроме нескольких id" —
// нужна почти каждой роли и для дневного голосования, и для ночного хода.
// Стратегия бота.
//
// Возвращает kNoTarget, если подходящих кандидатов не осталось.
PlayerId pick_random_target(const GameView& view, const std::vector<PlayerId>& exclude);

// Случайный элемент готового списка кандидатов (kNoTarget, если список пуст).
PlayerId pick_random_of(const std::vector<PlayerId>& candidates);

// Те же кандидаты (alive_players минус exclude), но выбирает человек: печатает
// подсказку и список, затем приостанавливается на `co_await input.read_line()`
// и переспрашивает при неверном вводе. Канал ввода/вывода берётся из
// view.input (его выставляет Ведущий).
//
// Параметры exclude и prompt берутся ПО ЗНАЧЕНИЮ, а не по ссылке: корутина
// хранит их в своём кадре и переживает приостановку, а ссылка на временный
// объект вызывающего к этому моменту могла бы уже «умереть».
//
// Возвращает kNoTarget, если кандидатов нет — консоль в этом случае не трогается.
// При исчерпанном вводе (EOF) возвращает первого кандидата, а не зацикливается.
// allow_skip добавляет вариант "-" (никого): для необязательных ходов.
Task<PlayerId> prompt_for_target_async(const Player& actor, const GameView& view, std::vector<PlayerId> exclude,
                                       std::string prompt, bool allow_skip = false);

// Выбор из уже готового списка кандидатов (например, из погибших этой ночью у
// Реаниматора). Имена берутся из view.alive_players и view.killed_tonight. При allow_skip
// исчерпанный ввод означает "никого", а не первого кандидата.
Task<PlayerId> prompt_from_candidates_async(const Player& actor, const GameView& view,
                                            std::vector<PlayerId> candidates, std::string prompt, bool allow_skip);

// Спрашивает человека выбрать одно из двух слов (например "shoot"/"check" у
// Комиссара). При EOF возвращает default_on_eof.
Task<bool> prompt_yes_no_async(const Player& actor, const GameView& view, std::string question, std::string yes_word,
                               std::string no_word, bool default_on_eof);

}  // namespace mafia::roles::detail
