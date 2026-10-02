#pragma once

#include <filesystem>
#include <vector>

#include "mafia/game_master.hpp"

namespace mafia {

// Пишет полный лог игры в .txt-файлы (п.3 задания): каждый раунд в свой файл
// и итоговый файл со статистикой по всем игрокам. Файлы всегда содержат
// ВСЮ информацию (роли, цели, проверки), независимо от режима объявлений
// Ведущего в консоли — это журнал для разбора партии, а не рассказ игрокам.
//
// Раскладка: <base_dir>/game_<время>/{setup.txt, round_01.txt, ..., summary.txt}
class GameLogger {
public:
    // Создаёт base_dir (со всеми промежуточными папками) и внутри неё уникальную
    // папку этой игры. Бросает std::filesystem::filesystem_error, если создать
    // директорию не удалось — решать, продолжать ли без логов, должен вызывающий.
    GameLogger(const GameMaster& master, const std::filesystem::path& base_dir);

    void log_setup();
    void log_day(int round, const DayReport& day);
    void log_night(int round, const NightReport& night);
    void write_summary(GameResult result);

    const std::filesystem::path& directory() const noexcept { return game_dir_; }

    // Созданные файлы логов в порядке имён.
    std::vector<std::filesystem::path> list_files() const;

private:
    enum class DeathCause { None, Executed, Killed };

    struct PlayerStats {
        int votes_cast = 0;
        int votes_received = 0;
        int votes_against_mafia = 0;
        int resurrections = 0;
        int death_round = 0;  // 0 — жив
        DeathCause death_cause = DeathCause::None;
    };

    std::filesystem::path round_file(int round) const;
    void write(const std::filesystem::path& path, const std::string& text, bool truncate);
    std::string describe(PlayerId id) const;

    const GameMaster& master_;
    std::filesystem::path game_dir_;
    std::vector<PlayerStats> stats_;  // индекс == PlayerId
    bool write_failed_reported_ = false;
};

}  // namespace mafia
