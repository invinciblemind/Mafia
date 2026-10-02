#include "mafia/game_logger.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace mafia {

namespace {

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d_%H%M%S");
    return out.str();
}

std::string_view result_text(GameResult result) {
    switch (result) {
        case GameResult::TownWins:
            return "Победили мирные жители";
        case GameResult::MafiaWins:
            return "Победила мафия";
        case GameResult::ManiacWins:
            return "Победил Маньяк";
        case GameResult::InProgress:
            return "Игра не завершена";
    }
    return "?";
}

std::string_view team_text(Team team) {
    switch (team) {
        case Team::Town:
            return "мирные";
        case Team::Mafia:
            return "мафия";
        case Team::Independent:
            return "одиночка";
    }
    return "?";
}

}  // namespace

GameLogger::GameLogger(const GameMaster& master, const fs::path& base_dir)
    : master_(master), stats_(master.all_players().size()) {
    // Две игры, запущенные в одну секунду, не должны писать в одну папку.
    const std::string stamp = timestamp();
    game_dir_ = base_dir / ("game_" + stamp);
    for (int suffix = 1; fs::exists(game_dir_); ++suffix) {
        game_dir_ = base_dir / ("game_" + stamp + "_" + std::to_string(suffix));
    }
    fs::create_directories(game_dir_);
}

fs::path GameLogger::round_file(int round) const {
    std::ostringstream name;
    name << "round_" << std::setw(2) << std::setfill('0') << round << ".txt";
    return game_dir_ / name.str();
}

void GameLogger::write(const fs::path& path, const std::string& text, bool truncate) {
    std::ofstream out(path, truncate ? (std::ios::out | std::ios::trunc) : (std::ios::out | std::ios::app));
    if (!out) {
        // Сбой записи не должен ронять игру: предупреждаем один раз и идём дальше.
        if (!write_failed_reported_) {
            write_failed_reported_ = true;
            std::cerr << "Не удалось записать лог в файл: " << path.string() << "\n";
        }
        return;
    }
    out << text;
}

std::string GameLogger::describe(PlayerId id) const {
    const SharedPtr<Player>& player = master_.all_players()[id];
    std::string text = player->name() + " (" + std::string(role_name(player->role())) + ")";
    if (player->is_interactive()) {
        text += " [человек]";
    }
    return text;
}

void GameLogger::log_setup() {
    std::ostringstream out;
    out << "=== Раздача ролей ===\n";
    out << "Игроков: " << master_.all_players().size() << "\n";
    for (const auto& player : master_.all_players()) {
        out << "  " << describe(player->id()) << " -- лагерь: " << team_text(player->team()) << "\n";
    }
    write(game_dir_ / "setup.txt", out.str(), /*truncate=*/true);
}

void GameLogger::log_day(int round, const DayReport& day) {
    std::ostringstream out;
    out << "=== Раунд " << round << " ===\n\n--- День ---\n";
    out << "Голосовали (живых в начале дня): " << day.votes.size() << "\n";

    std::map<PlayerId, int> tally;
    for (const auto& [voter, target] : day.votes) {
        out << "  " << describe(voter) << " -> ";
        ++stats_[voter].votes_cast;
        if (target == kNoTarget) {
            out << "воздержался\n";
            continue;
        }
        out << describe(target) << "\n";
        ++tally[target];
        ++stats_[target].votes_received;
        if (master_.all_players()[target]->team() == Team::Mafia) {
            ++stats_[voter].votes_against_mafia;
        }
    }

    out << "Подсчёт голосов:\n";
    std::vector<std::pair<PlayerId, int>> ranking(tally.begin(), tally.end());
    std::ranges::stable_sort(ranking, [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [target, count] : ranking) {
        out << "  " << describe(target) << ": " << count << "\n";
    }

    if (day.executed == kNoTarget) {
        out << "Итог: никто не казнён.\n";
    } else {
        if (day.was_tie) {
            out << "Ничья, Ведущий бросил монетку.\n";
        }
        out << "Итог: казнён " << describe(day.executed) << ".\n";
        stats_[day.executed].death_round = round;
        stats_[day.executed].death_cause = DeathCause::Executed;
    }
    if (day.sergeant_promoted != kNoTarget) {
        out << "Комиссар погиб: Сержант " << master_.all_players()[day.sergeant_promoted]->name()
            << " становится Комиссаром.\n";
    }
    out << "\n";
    write(round_file(round), out.str(), /*truncate=*/true);
}

void GameLogger::log_night(int round, const NightReport& night) {
    std::ostringstream out;
    out << "--- Ночь ---\n";
    if (night.block_target != kNoTarget) {
        out << "Вор блокировал: " << describe(night.block_target) << " -> "
            << (night.block_effective ? "действие не сработало" : "блокировать было нечего") << "\n";
    }
    if (night.healed != kNoTarget) {
        out << "Доктор лечил: " << describe(night.healed) << "\n";
    }
    if (night.mafia_target != kNoTarget) {
        out << "Мафия (решение Босса) выбрала целью: " << describe(night.mafia_target) << "\n";
    }
    if (night.maniac_target != kNoTarget) {
        out << "Маньяк выбрал целью: " << describe(night.maniac_target) << "\n";
    }
    if (night.commissar_check_target != kNoTarget) {
        out << "Комиссар проверил: " << describe(night.commissar_check_target) << " -> "
            << (night.commissar_check_result_mafia ? "мафия" : "не мафия") << "\n";
    }
    if (night.commissar_shot_target != kNoTarget) {
        out << "Комиссар стрелял в: " << describe(night.commissar_shot_target) << "\n";
    }

    if (night.killed.empty()) {
        out << "Итог: этой ночью никто не погиб.\n";
    } else {
        out << "Итог: погибли:\n";
        for (PlayerId id : night.killed) {
            out << "  " << describe(id) << "\n";
            stats_[id].death_round = round;
            stats_[id].death_cause = DeathCause::Killed;
        }
    }
    if (night.resurrected != kNoTarget) {
        out << "Реаниматор воскресил: " << describe(night.resurrected) << "\n";
        stats_[night.resurrected].death_round = 0;
        stats_[night.resurrected].death_cause = DeathCause::None;
        ++stats_[night.resurrected].resurrections;
    }
    if (night.sergeant_promoted != kNoTarget) {
        out << "Комиссар погиб: Сержант " << master_.all_players()[night.sergeant_promoted]->name()
            << " становится Комиссаром.\n";
    }
    out << "\n";
    write(round_file(round), out.str(), /*truncate=*/false);
}

void GameLogger::write_summary(GameResult result) {
    std::ostringstream out;
    out << "=== Итоги игры ===\n";
    out << "Результат: " << result_text(result) << "\n";
    out << "Раундов сыграно: " << master_.round_number() << "\n";
    out << "Игроков: " << master_.all_players().size() << "\n\n";

    out << "=== Статистика по игрокам ===\n";
    for (const auto& player : master_.all_players()) {
        const PlayerStats& stats = stats_[player->id()];
        out << describe(player->id()) << " -- лагерь: " << team_text(player->team()) << "\n";
        if (stats.death_cause == DeathCause::None) {
            out << "  статус: " << (player->is_alive() ? "жив" : "погиб") << "\n";
        } else {
            out << "  статус: погиб в раунде " << stats.death_round << " ("
                << (stats.death_cause == DeathCause::Executed ? "казнён днём" : "убит ночью") << ")\n";
        }
        out << "  голосов отдано: " << stats.votes_cast << " (из них против мафии: " << stats.votes_against_mafia
            << ")\n";
        out << "  голосов получено: " << stats.votes_received << "\n";
        if (stats.resurrections > 0) {
            out << "  воскрешён Реаниматором раз: " << stats.resurrections << "\n";
        }
    }
    write(game_dir_ / "summary.txt", out.str(), /*truncate=*/true);
}

std::vector<fs::path> GameLogger::list_files() const {
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it(game_dir_, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            files.push_back(it->path());
        }
    }
    std::ranges::sort(files);
    return files;
}

}  // namespace mafia
