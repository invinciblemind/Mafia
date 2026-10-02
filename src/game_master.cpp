#include "mafia/game_master.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <thread>

#include "mafia/game_view.hpp"
#include "mafia/role.hpp"
#include "mafia/roles/civilian.hpp"
#include "mafia/roles/commissar.hpp"
#include "mafia/roles/doctor.hpp"
#include "mafia/roles/mafia.hpp"
#include "mafia/roles/mafia_council.hpp"
#include "mafia/roles/maniac.hpp"

namespace mafia {

namespace {

// Отдельный от ролевых ботов генератор: этот используется только один раз,
// при раздаче ролей в конструкторе, на основном потоке — ему не нужна
// thread_local-изоляция, которая нужна генераторам внутри самих ролей
// (см. mafia/roles/role_utils.cpp).
std::mt19937& setup_rng() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

// Запускает decide(player) для каждого живого игрока в ОТДЕЛЬНОЙ std::thread
// и дожидается всех (join), прежде чем вернуть собранные результаты — п.2
// задания прямо требует, чтобы ход каждого игрока выполнялся в своей нити.
//
// Это безопасно без единого мьютекса, потому что:
//  - каждая нить пишет в СВОЙ отдельный индекс результирующего вектора
//    (results[i]), ни один индекс не используется двумя нитями;
//  - каждая нить вызывает act()/vote() только у СВОЕГО игрока — состояние
//    вроде last_healed_ у Доктора или checked_ у Комиссара принадлежит
//    только этому объекту и никогда не трогается другой нитью;
//  - GameView, который все нити читают параллельно, в этой фазе никем не
//    изменяется (только читается) — параллельное чтение неизменяемых данных
//    безопасно само по себе;
//  - SharedPtr использует атомарный счётчик ссылок (см. mafia/shared_ptr.hpp)
//    и выдерживает параллельное копирование/уничтожение;
//  - rng() внутри ролей — thread_local (см. mafia/roles/role_utils.cpp), то
//    есть у каждой нити свой генератор без общего состояния.
//
// Именно эта схема естественно подойдёт и для интерактивного игрока: пока
// боты почти мгновенно завершают свои нити, нить человека будет блокироваться
// на ожидании ввода с клавиатуры, а join() ниже просто подождёт её вместе со
// всеми остальными.
template <typename Decide>
auto collect_in_parallel(const std::vector<SharedPtr<Player>>& players, Decide decide)
    -> std::vector<decltype(decide(players.front()))> {
    using Result = decltype(decide(players.front()));

    std::vector<Result> results(players.size());
    std::vector<std::thread> threads;
    threads.reserve(players.size());

    for (std::size_t i = 0; i < players.size(); ++i) {
        threads.emplace_back([&players, &decide, &results, i]() { results[i] = decide(players[i]); });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    return results;
}

}  // namespace

GameMaster::GameMaster(int player_count, int mafia_divisor)
    : GameMaster(assign_roles(player_count, mafia_divisor)) {}

GameMaster::GameMaster(std::vector<SharedPtr<Player>> players) {
    std::size_t max_id = 0;
    for (const auto& player : players) {
        max_id = std::max(max_id, static_cast<std::size_t>(player->id()));
    }
    players_.assign(max_id + 1, SharedPtr<Player>{});
    for (auto& player : players) {
        players_[player->id()] = std::move(player);
    }
}

std::vector<SharedPtr<Player>> GameMaster::assign_roles(int player_count, int mafia_divisor) {
    int mafia_count = std::max(1, player_count / mafia_divisor);

    // Перемешиваем id случайным образом, затем нарезаем список на группы
    // нужных размеров: первые mafia_count id — Мафия, следующие три —
    // Доктор/Комиссар/Маньяк по одному, остаток — Мирные жители. Само
    // перемешивание и есть случайное распределение ролей; нарезка после
    // него — просто удобный способ разделить список на группы.
    std::vector<PlayerId> ids(static_cast<std::size_t>(player_count));
    std::iota(ids.begin(), ids.end(), PlayerId{0});
    std::shuffle(ids.begin(), ids.end(), setup_rng());

    auto name_for = [](PlayerId id) { return "Player" + std::to_string(id); };

    std::vector<SharedPtr<Player>> players(static_cast<std::size_t>(player_count));

    auto council = make_shared_ptr<roles::MafiaCouncil>();
    for (int i = 0; i < mafia_count; ++i) {
        council->members.push_back(ids[static_cast<std::size_t>(i)]);
    }

    std::size_t cursor = 0;
    for (int i = 0; i < mafia_count; ++i, ++cursor) {
        PlayerId id = ids[cursor];
        players[id] = make_shared_ptr<roles::Mafia>(id, name_for(id), council);
    }
    {
        PlayerId id = ids[cursor++];
        players[id] = make_shared_ptr<roles::Doctor>(id, name_for(id));
    }
    {
        PlayerId id = ids[cursor++];
        players[id] = make_shared_ptr<roles::Commissar>(id, name_for(id));
    }
    {
        PlayerId id = ids[cursor++];
        players[id] = make_shared_ptr<roles::Maniac>(id, name_for(id));
    }
    for (; cursor < ids.size(); ++cursor) {
        PlayerId id = ids[cursor];
        players[id] = make_shared_ptr<roles::Civilian>(id, name_for(id));
    }

    return players;
}

std::vector<SharedPtr<Player>> GameMaster::alive_players() const {
    std::vector<SharedPtr<Player>> alive;
    alive.reserve(players_.size());
    for (const auto& player : players_) {
        if (player->is_alive()) {
            alive.push_back(player);
        }
    }
    return alive;
}

DayReport GameMaster::play_day() {
    ++round_number_;
    GameView view{alive_players(), round_number_};

    DayReport report;
    std::map<PlayerId, int> tally;

    std::vector<PlayerId> votes = collect_in_parallel(
        view.alive_players, [&view](const SharedPtr<Player>& player) { return player->vote(view); });

    for (std::size_t i = 0; i < view.alive_players.size(); ++i) {
        PlayerId voter = view.alive_players[i]->id();
        PlayerId target = votes[i];
        report.votes.emplace_back(voter, target);
        if (target != kNoTarget) {
            ++tally[target];
        }
    }

    if (!tally.empty()) {
        int max_votes = 0;
        for (const auto& [target, count] : tally) {
            max_votes = std::max(max_votes, count);
        }

        std::vector<PlayerId> top_candidates;
        for (const auto& [target, count] : tally) {
            if (count == max_votes) {
                top_candidates.push_back(target);
            }
        }

        report.was_tie = top_candidates.size() > 1;

        // При ничьей бросаем монетку (один из двух вариантов, явно
        // допускаемых правилами; второй — "никто не выбывает" — мы не
        // выбрали, чтобы голосование гарантированно двигало игру вперёд).
        PlayerId executed =
            top_candidates.size() == 1
                ? top_candidates.front()
                : top_candidates[std::uniform_int_distribution<std::size_t>(
                      0, top_candidates.size() - 1)(setup_rng())];

        players_[executed]->kill();
        report.executed = executed;
    }

    return report;
}

NightReport GameMaster::play_night() {
    // round_number_ НЕ увеличиваем здесь: ночь относится к тому же раунду,
    // что и предшествующий ей день (по правилам день всегда идёт первым;
    // play_day() уже увеличил счётчик раунда перед вызовом play_night()).
    GameView view{alive_players(), round_number_};

    NightReport report;
    std::set<PlayerId> kill_targets;

    std::vector<NightAction> actions = collect_in_parallel(
        view.alive_players, [&view](const SharedPtr<Player>& player) { return player->act(view); });

    for (std::size_t i = 0; i < view.alive_players.size(); ++i) {
        const SharedPtr<Player>& player = view.alive_players[i];
        const NightAction& action = actions[i];
        switch (action.type) {
            case ActionType::Heal:
                report.healed = action.target;
                break;
            case ActionType::Kill:
                if (action.target != kNoTarget) {
                    kill_targets.insert(action.target);
                    if (player->role() == Role::Mafia) {
                        report.mafia_target = action.target;
                    } else if (player->role() == Role::Maniac) {
                        report.maniac_target = action.target;
                    }
                }
                break;
            case ActionType::Check:
                report.commissar_check_target = action.target;
                if (action.target != kNoTarget) {
                    // Правило: проверка Маньяка показывает его мирным. Это
                    // получается само собой — сверяем именно role() ==
                    // Role::Mafia, а не team(); у Маньяка role() == Maniac,
                    // значит условие ниже для него всегда false.
                    report.commissar_check_result_mafia = players_[action.target]->role() == Role::Mafia;
                }
                break;
            case ActionType::Shoot:
                report.commissar_shot_target = action.target;
                if (action.target != kNoTarget) {
                    kill_targets.insert(action.target);
                }
                break;
            case ActionType::None:
                break;
        }
    }

    // Лечение Доктора блокирует ЛЮБОЕ число одновременных покушений на ту же
    // цель за эту ночь (например, если и Мафия, и Маньяк независимо выбрали
    // одного и того же человека — Доктор спасает ото всех разом).
    if (report.healed != kNoTarget) {
        kill_targets.erase(report.healed);
    }

    for (PlayerId id : kill_targets) {
        players_[id]->kill();
        report.killed.push_back(id);
    }

    return report;
}

RoundReport GameMaster::play_round() {
    RoundReport report;
    report.day = play_day();
    if (check_winner() == GameResult::InProgress) {
        report.night = play_night();
        report.night_played = true;
    }
    return report;
}

GameResult GameMaster::check_winner() const {
    int mafia_alive = 0;
    int maniac_alive = 0;
    int town_alive = 0;

    for (const auto& player : players_) {
        if (!player->is_alive()) {
            continue;
        }
        switch (player->team()) {
            case Team::Mafia:
                ++mafia_alive;
                break;
            case Team::Independent:
                ++maniac_alive;  // пока единственная independent-роль — Маньяк
                break;
            case Team::Town:
                ++town_alive;
                break;
        }
    }

    if (mafia_alive == 0 && maniac_alive == 0) {
        return GameResult::TownWins;
    }
    // <= 1, а не == 1: Маньяк побеждает и "один на один с мирным", и в
    // ситуации, когда мирных вообще не осталось (например, мафия ночью
    // убивает последнего мирного, а Маньяк в ту же ночь убивает последнего
    // мафиози — тогда town_alive сразу становится 0, а не 1). При == 1 эта
    // ситуация ошибочно оставляла бы игру в состоянии InProgress навсегда,
    // потому что действовать после этого уже некому.
    if (maniac_alive > 0 && mafia_alive == 0 && town_alive <= 1) {
        return GameResult::ManiacWins;
    }

    // Трактовка п.5 правил мафии (неоднозначная формулировка про равенство
    // числа мафии и мирных "один из которых Маньяк"): "не-мафия" в целом —
    // это town_alive + maniac_alive. При строгом численном преимуществе
    // мафия побеждает всегда (п.4, первая часть правила). При РАВЕНСТВЕ
    // мафии и "не-мафии" — побеждает, только если Маньяка уже нет: если он
    // жив, именно он и "смещает" равенство, из-за чего правило 5 явно
    // откладывает исход до смерти мафии или Маньяка.
    int non_mafia_alive = town_alive + maniac_alive;
    if (mafia_alive > 0) {
        if (mafia_alive > non_mafia_alive) {
            return GameResult::MafiaWins;
        }
        if (mafia_alive == non_mafia_alive && maniac_alive == 0) {
            return GameResult::MafiaWins;
        }
    }
    return GameResult::InProgress;
}

}  // namespace mafia
