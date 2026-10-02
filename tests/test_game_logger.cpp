#include "mafia/game_logger.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

using mafia::DayReport;
using mafia::GameLogger;
using mafia::GameMaster;
using mafia::GameResult;
using mafia::GameView;
using mafia::kNoTarget;
using mafia::make_shared_ptr;
using mafia::NightAction;
using mafia::NightReport;
using mafia::Player;
using mafia::PlayerId;
using mafia::Role;
using mafia::SharedPtr;

namespace {

class ScriptedPlayer : public Player {
public:
    ScriptedPlayer(PlayerId id, std::string name, Role role, PlayerId vote_for)
        : Player(id, std::move(name)), role_(role), vote_for_(vote_for) {}

    Role role() const noexcept override { return role_; }
    PlayerId vote(const GameView&) override { return vote_for_; }
    NightAction act(const GameView&) override { return NightAction{}; }

private:
    Role role_;
    PlayerId vote_for_;
};

fs::path make_temp_root() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("mafia_logger_test_" + std::to_string(stamp));
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

GameMaster make_scripted_game() {
    // M(0) голосует за 1, C1(1) за 2, C2(2) за 1 -> казнят C1.
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<ScriptedPlayer>(0, "M", Role::Mafia, 1),
        make_shared_ptr<ScriptedPlayer>(1, "C1", Role::Civilian, 2),
        make_shared_ptr<ScriptedPlayer>(2, "C2", Role::Civilian, 1),
    };
    return GameMaster(std::move(players));
}

void test_creates_missing_nested_directories() {
    fs::path root = make_temp_root();
    fs::path base = root / "deeply" / "nested" / "logs";  // ни одной из папок ещё нет
    assert(!fs::exists(root));

    GameMaster gm = make_scripted_game();
    GameLogger logger(gm, base);

    assert(fs::is_directory(base));
    assert(fs::is_directory(logger.directory()));
    fs::remove_all(root);
}

void test_two_loggers_get_distinct_directories() {
    fs::path root = make_temp_root();
    GameMaster gm = make_scripted_game();
    GameLogger first(gm, root);
    GameLogger second(gm, root);  // та же секунда — папка не должна совпасть

    assert(first.directory() != second.directory());
    fs::remove_all(root);
}

void test_round_files_setup_and_summary_content() {
    fs::path root = make_temp_root();
    GameMaster gm = make_scripted_game();
    GameLogger logger(gm, root);

    logger.log_setup();
    DayReport day = gm.play_day();
    logger.log_day(gm.round_number(), day);

    NightReport night;
    night.killed = {0};
    night.mafia_target = 0;
    logger.log_night(gm.round_number(), night);
    logger.write_summary(GameResult::MafiaWins);

    assert(fs::exists(logger.directory() / "setup.txt"));
    assert(fs::exists(logger.directory() / "round_01.txt"));
    assert(fs::exists(logger.directory() / "summary.txt"));

    std::string round_text = read_file(logger.directory() / "round_01.txt");
    assert(round_text.find("--- День ---") != std::string::npos);
    assert(round_text.find("--- Ночь ---") != std::string::npos);  // ночь дописана в тот же файл
    assert(round_text.find("M (Мафия) -> C1 (Мирный житель)") != std::string::npos);  // кто за кого
    assert(round_text.find("казнён C1") != std::string::npos);

    std::string summary = read_file(logger.directory() / "summary.txt");
    assert(summary.find("Победила мафия") != std::string::npos);
    assert(summary.find("голосов получено: 2") != std::string::npos);  // C1 получил 2 голоса
    assert(summary.find("казнён днём") != std::string::npos);
    assert(summary.find("убит ночью") != std::string::npos);

    auto files = logger.list_files();
    assert(files.size() == 3);

    fs::remove_all(root);
}

void test_each_round_goes_to_its_own_file() {
    fs::path root = make_temp_root();
    GameMaster gm = make_scripted_game();
    GameLogger logger(gm, root);

    logger.log_day(1, DayReport{});
    logger.log_day(2, DayReport{});

    assert(fs::exists(logger.directory() / "round_01.txt"));
    assert(fs::exists(logger.directory() / "round_02.txt"));
    fs::remove_all(root);
}

void test_throws_filesystem_error_when_directory_cannot_be_created() {
    fs::path root = make_temp_root();
    fs::create_directories(root);
    fs::path blocker = root / "not_a_directory";
    std::ofstream(blocker) << "обычный файл";  // под файлом папку создать нельзя

    GameMaster gm = make_scripted_game();
    bool threw = false;
    try {
        GameLogger logger(gm, blocker / "logs");
    } catch (const fs::filesystem_error&) {
        threw = true;
    }
    assert(threw);
    fs::remove_all(root);
}

}  // namespace

int main() {
    test_creates_missing_nested_directories();
    test_two_loggers_get_distinct_directories();
    test_round_files_setup_and_summary_content();
    test_each_round_goes_to_its_own_file();
    test_throws_filesystem_error_when_directory_cannot_be_created();
    std::cout << "All GameLogger tests passed.\n";
    return 0;
}
