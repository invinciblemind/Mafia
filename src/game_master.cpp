#include "mafia/game_master.hpp"

#include <algorithm>
#include <numeric>
#include <random>
#include <set>
#include <string>

#include "mafia/game_view.hpp"
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

NightReport GameMaster::play_night() {
    ++round_number_;
    GameView view{alive_players(), round_number_};

    NightReport report;
    std::set<PlayerId> kill_targets;

    for (const auto& player : view.alive_players) {
        NightAction action = player->act(view);
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

}  // namespace mafia
