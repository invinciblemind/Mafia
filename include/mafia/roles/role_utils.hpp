#pragma once

#include <iostream>
#include <string_view>
#include <vector>

#include "mafia/action.hpp"
#include "mafia/game_view.hpp"
#include "mafia/player.hpp"

namespace mafia::roles::detail {

// Общая логика "выбрать случайного живого игрока, кроме нескольких id" —
// нужна почти каждой роли и для дневного голосования, и для ночного хода.
// Резервная стратегия для бота (и fallback, если интерактивный ввод почему-то
// недоступен).
//
// Возвращает kNoTarget, если подходящих кандидатов не осталось.
PlayerId pick_random_target(const GameView& view, const std::vector<PlayerId>& exclude);

// То же самое, что pick_random_target с точки зрения набора кандидатов (те
// же alive_players, то же exclude), но решение принимает не ГСЧ, а человек
// за консолью: печатает prompt и список кандидатов, читает id из потока
// ввода, переспрашивает при некорректном вводе. Потоки параметризованы
// (а не жёстко std::cin/std::cout), чтобы это можно было протестировать
// автоматически, подставив std::istringstream вместо реальной клавиатуры.
//
// Возвращает kNoTarget, если подходящих кандидатов не осталось — ровно как
// pick_random_target, консоль в этом случае не спрашивается вообще.
PlayerId prompt_for_target(const Player& actor, const GameView& view, const std::vector<PlayerId>& exclude,
                           std::string_view prompt, std::istream& in = std::cin,
                           std::ostream& out = std::cout);

// Спрашивает человека выбрать один из двух текстовых вариантов (например,
// "check"/"shoot" для хода Комиссара). Переспрашивает при нераспознанном
// ответе; если поток ввода исчерпан — возвращает default_on_eof, не уходя в
// бесконечный цикл попыток чтения (см. реализацию prompt_for_target).
bool prompt_yes_no(const Player& actor, std::string_view question, std::string_view yes_word,
                    std::string_view no_word, bool default_on_eof, std::istream& in = std::cin,
                    std::ostream& out = std::cout);

}  // namespace mafia::roles::detail
