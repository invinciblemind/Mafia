#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "mafia/game_master.hpp"
#include "mafia/role.hpp"

using mafia::DayReport;
using mafia::GameMaster;
using mafia::GameResult;
using mafia::kNoTarget;
using mafia::NightReport;
using mafia::Player;
using mafia::PlayerId;
using mafia::Role;
using mafia::SharedPtr;
using mafia::Team;

namespace {

struct Config {
    int player_count = 0;
    int mafia_divisor = 3;
    bool interactive = false;
    bool open_announcements = false;
    bool full_log = false;
};

void print_usage(const char* program_name) {
    std::cout
        << "Использование: " << program_name << " --players N [опции]\n\n"
        << "Обязательные параметры:\n"
        << "  --players N             количество игроков, N > 4\n\n"
        << "Необязательные параметры:\n"
        << "  --interactive           один из игроков управляется человеком\n"
        << "                          (пока только принимается — реальный ввод ещё не подключён)\n"
        << "  --open-announcements    Ведущий объявляет полный ролевой статус и детали ночи\n"
        << "                          (по умолчанию — закрытые объявления: только лагерь, без деталей)\n"
        << "  --full-log              выводить подробный внутренний лог раунда (голоса, цели действий)\n"
        << "  --mafia-divisor K       делитель k в формуле floor(N/k) для числа мафии, k >= 3 (по умолчанию 3)\n"
        << "  --help                  показать эту подсказку\n\n"
        << "Пример: " << program_name << " --players 10 --interactive --open-announcements --full-log\n";
}

std::optional<Config> parse_args(int argc, char** argv) {
    Config config;
    bool has_players = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--players") {
            if (i + 1 >= argc) {
                std::cerr << "--players требует значения\n";
                return std::nullopt;
            }
            config.player_count = std::atoi(argv[++i]);
            has_players = true;
        } else if (arg == "--mafia-divisor") {
            if (i + 1 >= argc) {
                std::cerr << "--mafia-divisor требует значения\n";
                return std::nullopt;
            }
            config.mafia_divisor = std::atoi(argv[++i]);
        } else if (arg == "--interactive") {
            config.interactive = true;
        } else if (arg == "--open-announcements") {
            config.open_announcements = true;
        } else if (arg == "--full-log") {
            config.full_log = true;
        } else {
            std::cerr << "Неизвестный параметр: " << arg << "\n";
            return std::nullopt;
        }
    }

    if (!has_players) {
        std::cerr << "Не задано --players N\n";
        return std::nullopt;
    }
    if (config.player_count <= 4) {
        std::cerr << "--players должен быть больше 4 (получено " << config.player_count << ")\n";
        return std::nullopt;
    }
    if (config.mafia_divisor < 3) {
        std::cerr << "--mafia-divisor должен быть не меньше 3 (получено " << config.mafia_divisor << ")\n";
        return std::nullopt;
    }

    return config;
}

std::string role_label(Role role) {
    switch (role) {
        case Role::Civilian:
            return "Мирный житель";
        case Role::Mafia:
            return "Мафия";
        case Role::Commissar:
            return "Комиссар";
        case Role::Doctor:
            return "Доктор";
        case Role::Maniac:
            return "Маньяк";
    }
    return "?";
}

// Правила закрытых объявлений дают ровно два публичных лагеря: "мирные" или
// "мафия" — Маньяк, хоть он формально Team::Independent, в эту бинарную
// формулировку не входит отдельным пунктом, поэтому здесь он публично
// объявляется наравне с мирными (вся внутренняя логика игры при этом
// по-прежнему различает его через team()/role(), это сугубо вопрос того,
// что Ведущий говорит игрокам вслух).
std::string closed_camp_label(Team team) {
    return team == Team::Mafia ? "мафия" : "мирные";
}

std::string player_name(const GameMaster& master, PlayerId id) {
    return master.all_players()[id]->name();
}

void print_setup(const GameMaster& master, const Config& config, std::optional<PlayerId> human) {
    std::cout << "=== Раздача ролей ===\n";
    std::cout << "Игроков: " << master.all_players().size() << "\n";
    if (config.full_log) {
        for (const auto& player : master.all_players()) {
            std::cout << "  " << player->name() << " (id " << player->id() << ") -- "
                       << role_label(player->role()) << "\n";
        }
    } else {
        std::cout << "(роли скрыты; запустите с --full-log, чтобы увидеть полную раздачу)\n";
    }
    std::cout << "\n";

    if (human) {
        const SharedPtr<Player>& you = master.all_players()[*human];
        std::cout << "Вы играете за " << you->name() << " (id " << *human << "). Ваша роль: "
                   << role_label(you->role()) << ".\n"
                   << "В свой ход Ведущий будет показывать подсказку и список доступных целей по id.\n\n";
    }
}

void print_day_report(const DayReport& day, const GameMaster& master, const Config& config) {
    if (config.full_log) {
        std::cout << "Голоса:\n";
        for (const auto& [voter, target] : day.votes) {
            std::cout << "  " << player_name(master, voter) << " -> "
                       << (target == kNoTarget ? "воздержался" : player_name(master, target)) << "\n";
        }
    }

    if (day.executed == kNoTarget) {
        std::cout << "По итогам голосования никто не выбывает.\n\n";
        return;
    }

    if (day.was_tie) {
        std::cout << "Голосование завершилось вничью, Ведущий бросает монетку.\n";
    }

    const SharedPtr<Player>& executed = master.all_players()[day.executed];
    std::cout << "Казнён по результатам голосования: " << executed->name();
    if (config.open_announcements) {
        std::cout << " -- роль: " << role_label(executed->role());
    } else {
        std::cout << " -- лагерь: " << closed_camp_label(executed->team());
    }
    std::cout << "\n\n";
}

void print_night_report(const NightReport& night, const GameMaster& master, const Config& config) {
    if (config.full_log) {
        std::cout << "Подробности ночи (внутренний лог):\n";
        if (night.healed != kNoTarget) {
            std::cout << "  Доктор лечил: " << player_name(master, night.healed) << "\n";
        }
        if (night.mafia_target != kNoTarget) {
            std::cout << "  Мафия выбрала целью: " << player_name(master, night.mafia_target) << "\n";
        }
        if (night.maniac_target != kNoTarget) {
            std::cout << "  Маньяк выбрал целью: " << player_name(master, night.maniac_target) << "\n";
        }
        if (night.commissar_check_target != kNoTarget) {
            std::cout << "  Комиссар проверил: " << player_name(master, night.commissar_check_target)
                       << " -> " << (night.commissar_check_result_mafia ? "мафия" : "не мафия") << "\n";
        }
        if (night.commissar_shot_target != kNoTarget) {
            std::cout << "  Комиссар стрелял в: " << player_name(master, night.commissar_shot_target) << "\n";
        }
    }

    if (night.killed.empty()) {
        std::cout << "Этой ночью никто не погиб";
        // При открытых объявлениях разрешено называть причину (например,
        // кого спас Доктор). При закрытых — причина отсутствия убийства
        // держится в секрете, сообщается только сам факт.
        if (config.open_announcements && night.healed != kNoTarget) {
            std::cout << " (Доктор спас " << player_name(master, night.healed) << ")";
        }
        std::cout << ".\n\n";
        return;
    }

    std::cout << "Этой ночью погибли:\n";
    for (PlayerId id : night.killed) {
        const SharedPtr<Player>& victim = master.all_players()[id];
        std::cout << "  " << victim->name();
        if (config.open_announcements) {
            std::cout << " -- роль: " << role_label(victim->role());
        } else {
            std::cout << " -- лагерь: " << closed_camp_label(victim->team());
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

void print_result(GameResult result) {
    std::cout << "=== Игра окончена ===\n";
    switch (result) {
        case GameResult::TownWins:
            std::cout << "Победили мирные жители!\n";
            break;
        case GameResult::MafiaWins:
            std::cout << "Победила мафия!\n";
            break;
        case GameResult::ManiacWins:
            std::cout << "Победил Маньяк!\n";
            break;
        case GameResult::InProgress:
            break;
    }
}

// Итоговый состав группируется по лагерям (мирные, мафия, Маньяк); внутри
// группы сначала выжившие, затем погибшие, дальше по id.
void print_final_roster(const GameMaster& master) {
    std::vector<SharedPtr<Player>> sorted = master.all_players();
    std::ranges::sort(sorted, [](const SharedPtr<Player>& a, const SharedPtr<Player>& b) {
        if (a->team() != b->team()) {
            return a->team() < b->team();
        }
        if (a->is_alive() != b->is_alive()) {
            return a->is_alive();
        }
        return a->id() < b->id();
    });

    std::cout << "\nИтоговый состав:\n";
    bool has_group = false;
    Team current_team = Team::Town;
    for (const auto& player : sorted) {
        if (!has_group || player->team() != current_team) {
            current_team = player->team();
            has_group = true;
            switch (current_team) {
                case Team::Town:
                    std::cout << "Мирные жители:\n";
                    break;
                case Team::Mafia:
                    std::cout << "Мафия:\n";
                    break;
                case Team::Independent:
                    std::cout << "Одиночки:\n";
                    break;
            }
        }
        std::cout << "  " << (player->is_alive() ? "[жив]   " : "[погиб] ") << player->name() << " -- "
                   << role_label(player->role()) << "\n";
    }
}

// Сообщает человеку о его гибели один раз, сразу после фазы, в которой он
// выбыл, — иначе у него просто молча перестанут запрашиваться действия.
void announce_human_death_if_needed(const GameMaster& master, std::optional<PlayerId> human, bool& announced,
                                    bool during_day) {
    if (!human || announced || master.all_players()[*human]->is_alive()) {
        return;
    }
    announced = true;
    std::cout << "!!! Вы погибли ("
              << (during_day ? "казнены дневным голосованием" : "убиты этой ночью")
              << "). Роль: " << role_label(master.all_players()[*human]->role())
              << ". Дальше вы наблюдаете за игрой. !!!\n\n";
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::optional<Config> config = parse_args(argc, argv);
    if (!config) {
        print_usage(argv[0]);
        return 1;
    }

    GameMaster master(config->player_count, config->mafia_divisor);

    std::optional<PlayerId> human;
    if (config->interactive) {
        std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<std::size_t> dist(0, master.all_players().size() - 1);
        human = static_cast<PlayerId>(dist(rng));
        master.all_players()[*human]->set_interactive();
    }

    print_setup(master, *config, human);

    bool human_death_announced = false;

    // День и ночь запускаются раздельно (а не через play_round), чтобы итоги
    // дня печатались ДО того, как человеку предложат ночной ход.
    while (master.check_winner() == GameResult::InProgress) {
        std::cout << "=== День " << (master.round_number() + 1) << " ===\n";
        DayReport day = master.play_day();
        print_day_report(day, master, *config);
        announce_human_death_if_needed(master, human, human_death_announced, /*during_day=*/true);

        if (master.check_winner() != GameResult::InProgress) {
            break;
        }

        std::cout << "=== Ночь " << master.round_number() << " ===\n";
        NightReport night = master.play_night();
        print_night_report(night, master, *config);
        announce_human_death_if_needed(master, human, human_death_announced, /*during_day=*/false);
    }

    print_result(master.check_winner());
    print_final_roster(master);

    return 0;
}
