#include "mafia/game_master.hpp"

#include <algorithm>
#include <map>
#include <iterator>
#include <numeric>
#include <random>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

#include "mafia/concepts.hpp"
#include "mafia/game_view.hpp"
#include "mafia/role.hpp"
#include "mafia/roles/civilian.hpp"
#include "mafia/roles/commissar.hpp"
#include "mafia/roles/doctor.hpp"
#include "mafia/roles/mafia.hpp"
#include "mafia/roles/mafia_council.hpp"
#include "mafia/roles/maniac.hpp"
#include "mafia/roles/resuscitator.hpp"
#include "mafia/roles/sergeant.hpp"
#include "mafia/roles/thief.hpp"

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

namespace {

RoleConfig config_with_divisor(int mafia_divisor) {
    RoleConfig config;
    config.mafia_divisor = mafia_divisor;
    return config;
}

}  // namespace

GameMaster::GameMaster(int player_count, int mafia_divisor)
    : GameMaster(player_count, config_with_divisor(mafia_divisor)) {}

GameMaster::GameMaster(int player_count, const RoleConfig& config)
    : GameMaster(assign_roles(player_count, config)) {}

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

std::vector<SharedPtr<Player>> GameMaster::assign_roles(int player_count, const RoleConfig& config) {
    int mafia_count = std::max(1, player_count / config.mafia_divisor);
    const int special_count = int(config.doctor) + int(config.commissar) + int(config.maniac) +
                              int(config.sergeant) + int(config.resuscitator);
    if (mafia_count + special_count > player_count) {
        throw std::invalid_argument("для " + std::to_string(player_count) + " игроков слишком много ролей: нужно минимум " +
                                    std::to_string(mafia_count + special_count) + " (мафия: " +
                                    std::to_string(mafia_count) + ", особых ролей: " + std::to_string(special_count) +
                                    ")");
    }

    // Перемешиваем id случайным образом, затем нарезаем список на группы по
    // порядку: мафия (из них один Вор, если он включён — Вор занимает одно из
    // мест мафии по формуле, а не добавляется сверх неё), затем каждая
    // включённая особая роль, остаток — Мирные жители. Само перемешивание и
    // есть случайное распределение ролей.
    std::vector<PlayerId> ids(static_cast<std::size_t>(player_count));
    std::iota(ids.begin(), ids.end(), PlayerId{0});
    std::shuffle(ids.begin(), ids.end(), setup_rng());

    auto name_for = [](PlayerId id) { return "Player" + std::to_string(id); };

    std::vector<SharedPtr<Player>> players(static_cast<std::size_t>(player_count));
    std::size_t cursor = 0;

    auto council = make_shared_ptr<roles::MafiaCouncil>();
    for (int i = 0; i < mafia_count; ++i) {
        council->members.push_back(ids[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i < mafia_count; ++i, ++cursor) {
        PlayerId id = ids[cursor];
        if (config.thief && i == 0) {
            council->thieves.push_back(id);
            players[id] = make_player<roles::Thief>(id, name_for(id), council);
        } else {
            players[id] = make_player<roles::Mafia>(id, name_for(id), council);
        }
    }

    // Досье Комиссара общее с Сержантом (если оба в игре).
    auto intel = make_shared_ptr<roles::CommissarIntel>();

    if (config.doctor) {
        PlayerId id = ids[cursor++];
        players[id] = make_player<roles::Doctor>(id, name_for(id));
    }
    if (config.commissar) {
        PlayerId id = ids[cursor++];
        intel->commissar_id = id;
        players[id] = make_player<roles::Commissar>(id, name_for(id), intel);
    }
    if (config.sergeant) {
        PlayerId id = ids[cursor++];
        intel->sergeant_id = id;
        players[id] = make_player<roles::Sergeant>(id, name_for(id), intel);
    }
    if (config.resuscitator) {
        PlayerId id = ids[cursor++];
        players[id] = make_player<roles::Resuscitator>(id, name_for(id));
    }
    if (config.maniac) {
        PlayerId id = ids[cursor++];
        players[id] = make_player<roles::Maniac>(id, name_for(id));
    }
    for (; cursor < ids.size(); ++cursor) {
        PlayerId id = ids[cursor];
        players[id] = make_player<roles::Civilian>(id, name_for(id));
    }

    return players;
}

std::vector<SharedPtr<Player>> GameMaster::alive_players() const {
    std::vector<SharedPtr<Player>> alive;
    alive.reserve(players_.size());
    std::ranges::copy(players_ | std::views::filter([](const SharedPtr<Player>& player) { return player->is_alive(); }),
                      std::back_inserter(alive));
    return alive;
}

PlayerId GameMaster::promote_sergeant_if_needed() {
    for (SharedPtr<Player>& slot : players_) {
        if (!slot->is_alive()) {
            continue;
        }
        auto* sergeant = dynamic_cast<roles::Sergeant*>(slot.get());
        if (sergeant == nullptr) {
            continue;
        }
        // Копия досье: старый объект Sergeant исчезнет при замене слота.
        SharedPtr<roles::CommissarIntel> intel = sergeant->intel();
        if (intel->commissar_id == kNoTarget || players_[intel->commissar_id]->is_alive()) {
            continue;
        }

        PlayerId id = slot->id();
        std::string name = slot->name();
        bool interactive = slot->is_interactive();
        intel->commissar_id = id;
        intel->sergeant_id = kNoTarget;
        slot = make_player<roles::Commissar>(id, name, intel);
        slot->set_interactive(interactive);
        return id;
    }
    return kNoTarget;
}

DayReport GameMaster::play_day() {
    ++round_number_;
    GameView view{alive_players(), round_number_, &input_};

    DayReport report;
    std::map<PlayerId, int> tally;

    std::vector<PlayerId> votes;
    if (mode_ == ExecutionMode::Threads) {
        votes = collect_in_parallel(view.alive_players,
                                    [&view](const SharedPtr<Player>& player) { return player->vote(view); });
    } else {
        // Корутины: по задаче на игрока, всё в этом же потоке. Боты завершаются
        // сразу, человек приостанавливается на вводе — см. run_all.
        std::vector<Task<PlayerId>> tasks;
        tasks.reserve(view.alive_players.size());
        for (const SharedPtr<Player>& player : view.alive_players) {
            tasks.push_back(player->vote_async(view));
        }
        votes = run_all(std::move(tasks), input_);
    }

    for (std::size_t i = 0; i < view.alive_players.size(); ++i) {
        PlayerId voter = view.alive_players[i]->id();
        PlayerId target = votes[i];
        report.votes.emplace_back(voter, target);
        if (target != kNoTarget) {
            ++tally[target];
        }
    }

    if (!tally.empty()) {
        // Лидеры голосования: максимум голосов через ranges, затем фильтр по нему.
        auto votes_of = [](const auto& entry) { return entry.second; };
        const int max_votes = std::ranges::max(tally | std::views::transform(votes_of));

        std::vector<PlayerId> top_candidates;
        std::ranges::copy(tally | std::views::filter([max_votes](const auto& entry) { return entry.second == max_votes; }) |
                              std::views::transform([](const auto& entry) { return entry.first; }),
                          std::back_inserter(top_candidates));

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
        report.sergeant_promoted = promote_sergeant_if_needed();
    }

    return report;
}

NightReport GameMaster::play_night() {
    // round_number_ НЕ увеличиваем здесь: ночь относится к тому же раунду,
    // что и предшествующий ей день (по правилам день всегда идёт первым;
    // play_day() уже увеличил счётчик раунда перед вызовом play_night()).
    GameView view{alive_players(), round_number_, &input_};

    NightReport report;
    std::set<PlayerId> kill_targets;

    // Исполнение ночных ходов группы игроков: в нитях или корутинами.
    auto run_actions = [this](const std::vector<SharedPtr<Player>>& who, const GameView& context) {
        std::vector<NightAction> result;
        if (mode_ == ExecutionMode::Threads) {
            result = collect_in_parallel(who, [&context](const SharedPtr<Player>& player) { return player->act(context); });
        } else {
            std::vector<Task<NightAction>> tasks;
            tasks.reserve(who.size());
            for (const SharedPtr<Player>& player : who) {
                tasks.push_back(player->act_async(context));
            }
            result = run_all(std::move(tasks), input_);
        }
        return result;
    };

    // Ночь в два этапа: сначала ходят все одновременно, кроме тех, кто ходит
    // последним (Реаниматор) — им нужно знать итоги первого этапа.
    std::vector<SharedPtr<Player>> first_stage;
    std::vector<SharedPtr<Player>> last_stage;
    for (const SharedPtr<Player>& player : view.alive_players) {
        (player->acts_after_resolution() ? last_stage : first_stage).push_back(player);
    }

    std::vector<NightAction> actions = run_actions(first_stage, view);

    // Блокировка Вора действует до разрешения остальных ходов: все действия
    // совершаются одновременно, но у заблокированной особой роли мирного оно
    // просто не срабатывает. Блокировать мафию, Маньяка и обычных мирных
    // нечего: у них нет особых способностей мирной стороны.
    std::set<PlayerId> blocked;
    for (const NightAction& action : actions) {
        if (action.type == ActionType::Block && action.target != kNoTarget) {
            blocked.insert(action.target);
            report.block_target = action.target;
        }
    }
    auto is_cancelled_by_block = [&blocked](const Player& player, const NightAction& action) {
        return blocked.contains(player.id()) && player.team() == Team::Town && action.type != ActionType::None;
    };
    for (std::size_t i = 0; i < first_stage.size(); ++i) {
        if (is_cancelled_by_block(*first_stage[i], actions[i])) {
            actions[i] = NightAction{};
            report.block_effective = true;
        }
    }

    for (std::size_t i = 0; i < first_stage.size(); ++i) {
        const SharedPtr<Player>& player = first_stage[i];
        const NightAction& action = actions[i];
        switch (action.type) {
            case ActionType::Heal:
                report.healed = action.target;
                break;
            case ActionType::Kill:
                if (action.target != kNoTarget) {
                    kill_targets.insert(action.target);
                    if (player->team() == Team::Mafia) {
                        report.mafia_target = action.target;
                    } else if (player->team() == Team::Independent) {
                        report.maniac_target = action.target;
                    }
                }
                break;
            case ActionType::Check:
                report.commissar_check_target = action.target;
                if (action.target != kNoTarget) {
                    // Мафией считается любой игрок лагеря мафии (в том числе Вор);
                    // Маньяк — Independent, значит для него результат всегда
                    // "не мафия", как и требуют правила проверки Комиссара.
                    bool is_mafia = players_[action.target]->team() == Team::Mafia;
                    report.commissar_check_result_mafia = is_mafia;
                    if (auto* commissar = dynamic_cast<roles::Commissar*>(player.get())) {
                        commissar->record_check(action.target, is_mafia);  // досье Комиссара и Сержанта
                    }
                }
                break;
            case ActionType::Shoot:
                report.commissar_shot_target = action.target;
                if (action.target != kNoTarget) {
                    kill_targets.insert(action.target);
                }
                break;
            case ActionType::Block:      // уже учтено выше
            case ActionType::Resurrect:  // Реаниматор ходит на втором этапе
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

    // Второй этап: Реаниматор ходит последним, когда уже известно, кто
    // погиб этой ночью, и видит их имена (без ролей). Казнённых голосованием
    // и погибших прежними ночами среди них нет. Если его самого убили этой
    // ночью, он всё равно успевает сделать ход (как и остальные, кто ходил
    // одновременно) и вправе назвать в том числе себя.
    if (!last_stage.empty()) {
        GameView last_view{view.alive_players, round_number_, &input_};
        for (PlayerId id : report.killed) {
            last_view.killed_tonight.push_back(players_[id]);
        }
        std::vector<NightAction> last_actions = run_actions(last_stage, last_view);

        for (std::size_t i = 0; i < last_stage.size(); ++i) {
            const SharedPtr<Player>& player = last_stage[i];
            const NightAction& action = last_actions[i];
            if (is_cancelled_by_block(*player, action)) {
                report.block_effective = true;  // Вор заблокировал Реаниматора: воскрешения не будет
                continue;
            }
            bool was_killed_tonight = std::ranges::find(report.killed, action.target) != report.killed.end();
            if (action.type == ActionType::Resurrect && was_killed_tonight && !players_[action.target]->is_alive()) {
                players_[action.target]->revive();
                report.resurrected = action.target;
                if (auto* reviver = dynamic_cast<roles::Resuscitator*>(player.get())) {
                    reviver->note_revived(action.target);
                }
            }
        }
    }

    // Замена Комиссара проверяется в самом конце: убитого Комиссара могли
    // воскресить, и тогда Сержант остаётся Сержантом.
    report.sergeant_promoted = promote_sergeant_if_needed();
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
    // Живые игроки каждого лагеря — подсчёт через ranges::count_if.
    auto alive_in = [this](Team team) {
        return static_cast<int>(std::ranges::count_if(
            players_, [team](const SharedPtr<Player>& player) { return player->is_alive() && player->team() == team; }));
    };
    const int mafia_alive = alive_in(Team::Mafia);
    const int maniac_alive = alive_in(Team::Independent);  // пока единственная independent-роль — Маньяк
    const int town_alive = alive_in(Team::Town);

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
