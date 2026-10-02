#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mafia/game_logger.hpp"
#include "mafia/game_master.hpp"
#include "mafia/game_view.hpp"
#include "mafia/role_config.hpp"
#include "mafia/roles/mafia.hpp"
#include "mafia/role.hpp"

using mafia::DayReport;
using mafia::GameLogger;
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
    std::optional<int> mafia_divisor;                    // переопределяет значение из конфигурации ролей
    std::optional<std::filesystem::path> roles_config;  // YAML с набором ролей
    bool interactive = false;
    bool open_announcements = false;
    bool full_log = false;
    bool file_log = true;
    bool coroutines = false;
    std::filesystem::path log_dir = "logs";
};

void print_usage(const char* program_name) {
    std::cout
        << "Использование: " << program_name << " --players N [опции]\n\n"
        << "Обязательные параметры:\n"
        << "  --players N             количество игроков, N > 4\n\n"
        << "Необязательные параметры:\n"
        << "  --interactive           один из игроков (случайный) управляется человеком\n"
        << "  --roles-config FILE     YAML-файл с набором ролей (по умолчанию: мафия, мирные, доктор,\n"
        << "                          комиссар, маньяк); примеры лежат в config/\n"
        << "  --open-announcements    Ведущий объявляет полный ролевой статус и детали ночи\n"
        << "                          (по умолчанию — закрытые объявления: только лагерь, без деталей)\n"
        << "  --full-log              выводить подробный внутренний лог раунда (голоса, цели действий)\n"
        << "  --mafia-divisor K       делитель k в формуле floor(N/k) для числа мафии, k >= 3\n                          (по умолчанию 3 или значение из --roles-config)\n"
        << "  --coroutines            исполнять ходы игроков корутинами в одном потоке вместо нитей\n"
        << "  --log-dir DIR           куда писать файловые логи игры (по умолчанию ./logs)\n"
        << "  --no-file-log           не писать файловые логи\n"
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
        } else if (arg == "--roles-config") {
            if (i + 1 >= argc) {
                std::cerr << "--roles-config требует значения\n";
                return std::nullopt;
            }
            config.roles_config = argv[++i];
        } else if (arg == "--interactive") {
            config.interactive = true;
        } else if (arg == "--open-announcements") {
            config.open_announcements = true;
        } else if (arg == "--full-log") {
            config.full_log = true;
        } else if (arg == "--coroutines") {
            config.coroutines = true;
        } else if (arg == "--no-file-log") {
            config.file_log = false;
        } else if (arg == "--log-dir") {
            if (i + 1 >= argc) {
                std::cerr << "--log-dir требует значения\n";
                return std::nullopt;
            }
            config.log_dir = argv[++i];
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
    if (config.mafia_divisor && *config.mafia_divisor < 3) {
        std::cerr << "--mafia-divisor должен быть не меньше 3 (получено " << *config.mafia_divisor << ")\n";
        return std::nullopt;
    }

    return config;
}

std::string role_label(Role role) {
    return std::string(mafia::role_name(role));
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

// Какие роли участвуют в партии — это публичная информация, как и их число.
std::string roles_in_game(const mafia::RoleConfig& roles) {
    std::string text = "Мафия, Мирный житель";
    auto add = [&text](bool enabled, Role role) {
        if (enabled) {
            text += ", " + role_label(role);
        }
    };
    add(roles.doctor, Role::Doctor);
    add(roles.commissar, Role::Commissar);
    add(roles.maniac, Role::Maniac);
    add(roles.sergeant, Role::Sergeant);
    add(roles.resuscitator, Role::Resuscitator);
    add(roles.thief, Role::Thief);
    return text;
}

void print_setup(const GameMaster& master, const Config& config, const mafia::RoleConfig& roles,
                 std::optional<PlayerId> human) {
    std::cout << "=== Раздача ролей ===\n";
    std::cout << "Игроков: " << master.all_players().size() << "\n";
    std::cout << "Роли в игре: " << roles_in_game(roles) << "\n";
    std::cout << "Ходы игроков исполняются: " << (config.coroutines ? "корутинами (один поток)" : "в отдельных нитях")
              << "\n";
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
        if (night.block_target != kNoTarget) {
            std::cout << "  Вор блокировал: " << player_name(master, night.block_target) << " -> "
                       << (night.block_effective ? "действие не сработало" : "блокировать было нечего") << "\n";
        }
        if (night.resurrected != kNoTarget) {
            std::cout << "  Реаниматор воскресил: " << player_name(master, night.resurrected) << "\n";
        }
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

    // Воскрешённый Реаниматором входит в night.killed, но утром не числится
    // погибшим: смерть была отменена до рассвета. Что это сделал Реаниматор,
    // при открытых объявлениях говорится вслух, при закрытых — секрет.
    std::vector<PlayerId> dead_in_morning;
    std::ranges::copy_if(night.killed, std::back_inserter(dead_in_morning),
                         [&night](PlayerId id) { return id != night.resurrected; });
    if (config.open_announcements && night.resurrected != kNoTarget) {
        const SharedPtr<Player>& returned = master.all_players()[night.resurrected];
        std::cout << "Реаниматор воскресил " << returned->name() << " -- роль: " << role_label(returned->role())
                  << ".\n";
    }
    if (config.open_announcements && night.block_effective) {
        std::cout << "Вор заблокировал " << player_name(master, night.block_target) << ": ночное действие не сработало.\n";
    }

    if (dead_in_morning.empty()) {
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
    for (PlayerId id : dead_in_morning) {
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

// Банда мафии игрока-человека (nullptr, если он не мафия). Через неё узнаём
// состав команды и текущего Босса теми же средствами, что и сами боты.
const mafia::roles::MafiaCouncil* human_council(const GameMaster& master, std::optional<PlayerId> human) {
    if (!human) {
        return nullptr;
    }
    const auto* mafia_player = dynamic_cast<const mafia::roles::Mafia*>(master.all_players()[*human].get());
    return mafia_player ? mafia_player->council().get() : nullptr;
}

PlayerId current_boss(const GameMaster& master, const mafia::roles::MafiaCouncil& council) {
    return council.current_boss(mafia::GameView{master.alive_players(), master.round_number()});
}

// Приватное сообщение мафии-человеку в начале игры: вся его команда.
void print_mafia_team(const GameMaster& master, PlayerId human, const mafia::roles::MafiaCouncil& council) {
    PlayerId boss = current_boss(master, council);
    std::cout << "Ваша команда (мафия) -- это известно только вам и вашим сообщникам:\n";
    for (PlayerId member : council.members) {
        std::cout << "  " << player_name(master, member);
        if (member == human) {
            std::cout << " (это вы)";
        }
        Role role = master.all_players()[member]->role();
        if (role != Role::Mafia) {
            std::cout << " -- " << role_label(role);
        }
        if (member == boss) {
            std::cout << " -- Босс банды";
        }
        std::cout << "\n";
    }
    std::cout << "Ночное убийство озвучивает Босс; остальные обычные члены банды в ночь не ходят, "
                 "а Вор (если он есть) блокирует способности мирного.\n\n";
}

// Перед ночью напоминает мафии-человеку, что у него за ход. Если прежний
// Босс погиб и мандат перешёл к человеку, говорит об этом отдельно.
void announce_mafia_night(const GameMaster& master, PlayerId human, const mafia::roles::MafiaCouncil& council,
                          PlayerId& previous_boss) {
    if (!master.all_players()[human]->is_alive()) {
        return;
    }
    PlayerId boss = current_boss(master, council);
    if (boss == human) {
        if (previous_boss != human) {
            std::cout << "[Только для вас] Прежний Босс банды погиб -- теперь Босс вы.\n";
        }
        std::cout << "[Только для вас] Вы -- Босс: этой ночью решение об убийстве за вами.\n";
    } else if (master.all_players()[human]->role() == Role::Thief) {
        std::cout << "[Только для вас] Босс банды -- " << player_name(master, boss)
                  << ", он озвучивает убийство. Ваш ход -- блокировка способностей мирного.\n";
    } else {
        std::cout << "[Только для вас] Босс банды -- " << player_name(master, boss)
                  << ", он озвучивает решение мафии. Вы этой ночью не ходите: банда уже договорилась.\n";
    }
    previous_boss = boss;
}

std::optional<PlayerId> find_alive_with_role(const GameMaster& master, Role role) {
    for (const auto& player : master.all_players()) {
        if (player->is_alive() && player->role() == role) {
            return player->id();
        }
    }
    return std::nullopt;
}

// Комиссар и Сержант знают друг друга: об этом им сообщают в начале игры.
void print_commissar_pair_knowledge(const GameMaster& master, std::optional<PlayerId> human) {
    if (!human) {
        return;
    }
    Role role = master.all_players()[*human]->role();
    if (role == Role::Commissar) {
        if (auto sergeant = find_alive_with_role(master, Role::Sergeant)) {
            std::cout << "[Только для вас] Ваш Сержант -- " << player_name(master, *sergeant)
                      << ". Он знает, кто вы, видит результаты ваших проверок и заменит вас, если вы погибнете.\n\n";
        }
    } else if (role == Role::Sergeant) {
        if (auto commissar = find_alive_with_role(master, Role::Commissar)) {
            std::cout << "[Только для вас] Комиссар -- " << player_name(master, *commissar)
                      << ". Вы будете узнавать результаты его проверок; если он погибнет, вы займёте его место.\n\n";
        }
    }
}

// Всё, что Ведущий сообщает человеку приватно по итогам ночи: результаты
// проверок Комиссара (ему и его Сержанту), провал заблокированного хода,
// воскрешение, повышение Сержанта. Печатается независимо от --full-log и
// режима объявлений. Историю проверок копим, чтобы напоминать её по утрам.
void report_night_private(const NightReport& night, const GameMaster& master, std::optional<PlayerId> human,
                          std::vector<std::string>& check_history) {
    if (!human) {
        return;
    }
    const SharedPtr<Player>& you = master.all_players()[*human];
    // Сержант, ставший этой ночью Комиссаром, ночью ещё был Сержантом.
    Role night_role = (night.sergeant_promoted == *human) ? Role::Sergeant : you->role();

    if (night.block_effective && night.block_target == *human) {
        std::cout << "[Только для вас] Этой ночью ваше ночное действие не сработало.\n\n";
    }

    if (night_role == Role::Commissar || (night_role == Role::Sergeant && you->is_alive())) {
        bool is_commissar = night_role == Role::Commissar;
        if (night.commissar_check_target != kNoTarget) {
            std::string line = player_name(master, night.commissar_check_target) + " -- " +
                               (night.commissar_check_result_mafia ? "МАФИЯ" : "не мафия");
            std::cout << "[Только для вас] " << (is_commissar ? "Результат вашей проверки: " : "Комиссар проверил: ")
                      << line << ".\n\n";
            check_history.push_back(line);
        }
        if (night.commissar_shot_target != kNoTarget) {
            std::cout << "[Только для вас] " << (is_commissar ? "Вы стреляли в " : "Комиссар стрелял в ")
                      << player_name(master, night.commissar_shot_target) << ".\n\n";
        }
    }

    if (night_role == Role::Resuscitator && night.resurrected != kNoTarget) {
        std::cout << "[Только для вас] Вы воскресили " << player_name(master, night.resurrected) << ".\n\n";
    }
}

// Сообщает Сержанту-человеку, что он теперь Комиссар (день или ночь).
void announce_promotion(PlayerId promoted, std::optional<PlayerId> human,
                        const std::vector<std::string>& check_history) {
    if (promoted == kNoTarget || !human || promoted != *human) {
        return;
    }
    std::cout << "[Только для вас] Комиссар погиб -- теперь вы Комиссар: каждую ночь можно проверять или стрелять.\n";
    if (!check_history.empty()) {
        std::cout << "Известные вам результаты проверок:\n";
        for (const std::string& line : check_history) {
            std::cout << "  " << line << "\n";
        }
    }
    std::cout << "\n";
}

void remind_commissar_checks(const GameMaster& master, std::optional<PlayerId> human,
                             const std::vector<std::string>& check_history) {
    if (!human || check_history.empty() || !master.all_players()[*human]->is_alive()) {
        return;
    }
    Role role = master.all_players()[*human]->role();
    if (role != Role::Commissar && role != Role::Sergeant) {
        return;
    }
    std::cout << "[Только для вас] "
              << (role == Role::Commissar ? "Ваши прошлые проверки:" : "Результаты проверок Комиссара:") << "\n";
    for (const std::string& line : check_history) {
        std::cout << "  " << line << "\n";
    }
    std::cout << "\n";
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

    mafia::RoleConfig role_config = mafia::default_role_config();
    if (config->roles_config) {
        try {
            role_config = mafia::load_role_config(*config->roles_config);
        } catch (const mafia::ConfigError& error) {
            std::cerr << "Ошибка конфигурации ролей: " << error.what() << "\n";
            return 1;
        }
    }
    if (config->mafia_divisor) {
        role_config.mafia_divisor = *config->mafia_divisor;
    }
    for (const std::string& warning : mafia::role_config_warnings(role_config)) {
        std::cerr << "Предупреждение: " << warning << "\n";
    }

    std::optional<GameMaster> master_storage;
    try {
        master_storage.emplace(config->player_count, role_config);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Ошибка: " << error.what() << "\n";
        return 1;
    }
    GameMaster& master = *master_storage;
    master.set_execution_mode(config->coroutines ? mafia::ExecutionMode::Coroutines : mafia::ExecutionMode::Threads);

    std::optional<PlayerId> human;
    if (config->interactive) {
        std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<std::size_t> dist(0, master.all_players().size() - 1);
        human = static_cast<PlayerId>(dist(rng));
        master.all_players()[*human]->set_interactive();
    }

    print_setup(master, *config, role_config, human);
    print_commissar_pair_knowledge(master, human);

    const mafia::roles::MafiaCouncil* council = human_council(master, human);
    PlayerId previous_boss = kNoTarget;
    if (council) {
        print_mafia_team(master, *human, *council);
        previous_boss = current_boss(master, *council);
    }
    std::vector<std::string> commissar_checks;

    std::optional<GameLogger> logger;
    if (config->file_log) {
        try {
            logger.emplace(master, config->log_dir);
            logger->log_setup();
        } catch (const std::filesystem::filesystem_error& error) {
            std::cerr << "Не удалось создать директорию логов (" << error.what()
                      << ").\nИгра продолжится без файлового логирования.\n\n";
            logger.reset();
        }
    }

    bool human_death_announced = false;

    // День и ночь запускаются раздельно (а не через play_round), чтобы итоги
    // дня печатались ДО того, как человеку предложат ночной ход.
    while (master.check_winner() == GameResult::InProgress) {
        std::cout << "=== День " << (master.round_number() + 1) << " ===\n";
        remind_commissar_checks(master, human, commissar_checks);
        DayReport day = master.play_day();
        print_day_report(day, master, *config);
        if (logger) {
            logger->log_day(master.round_number(), day);
        }
        announce_promotion(day.sergeant_promoted, human, commissar_checks);
        announce_human_death_if_needed(master, human, human_death_announced, /*during_day=*/true);

        if (master.check_winner() != GameResult::InProgress) {
            break;
        }

        std::cout << "=== Ночь " << master.round_number() << " ===\n";
        if (council) {
            announce_mafia_night(master, *human, *council, previous_boss);
        }
        NightReport night = master.play_night();
        report_night_private(night, master, human, commissar_checks);
        print_night_report(night, master, *config);
        if (logger) {
            logger->log_night(master.round_number(), night);
        }
        announce_promotion(night.sergeant_promoted, human, commissar_checks);
        announce_human_death_if_needed(master, human, human_death_announced, /*during_day=*/false);
    }

    print_result(master.check_winner());
    print_final_roster(master);

    if (logger) {
        logger->write_summary(master.check_winner());
        std::cout << "\nЛоги игры сохранены в " << std::filesystem::absolute(logger->directory()).string() << ":\n";
        for (const auto& file : logger->list_files()) {
            std::error_code ec;
            auto size = std::filesystem::file_size(file, ec);
            std::cout << "  " << file.filename().string();
            if (!ec) {
                std::cout << " (" << size << " Б)";
            }
            std::cout << "\n";
        }
    }

    return 0;
}
