#pragma once

#include <utility>
#include <vector>

#include "mafia/action.hpp"
#include "mafia/coroutines.hpp"
#include "mafia/player.hpp"
#include "mafia/role_config.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia {

// Итог одной ночи — для утреннего объявления Ведущего и для лога игры.
// kNoTarget/пустой killed означают "этого не случилось" (например, Комиссар
// мог ни разу не выстрелить — тогда commissar_shot_target == kNoTarget).
struct NightReport {
    std::vector<PlayerId> killed;               // кто умер этой ночью (после лечения)
    PlayerId healed = kNoTarget;                 // кого лечил Доктор
    PlayerId mafia_target = kNoTarget;           // коллективный выбор мафии (голос Босса)
    PlayerId maniac_target = kNoTarget;          // цель Маньяка
    PlayerId commissar_check_target = kNoTarget;
    bool commissar_check_result_mafia = false;   // осмысленно только если target != kNoTarget
    PlayerId commissar_shot_target = kNoTarget;

    PlayerId block_target = kNoTarget;  // кого пытался заблокировать Вор
    bool block_effective = false;       // блокировка сработала (у цели было ночное действие мирной роли)
    PlayerId resurrected = kNoTarget;   // кого воскресил Реаниматор (из погибших этой ночью; входит и в killed)
    PlayerId sergeant_promoted = kNoTarget;  // Сержант, ставший Комиссаром после гибели Комиссара
};

// Итог одного дневного голосования.
struct DayReport {
    // Кто за кого голосовал — пригодится файловому логированию (п.3 задания
    // прямо требует фиксировать "кто голосовал за кого").
    std::vector<std::pair<PlayerId, PlayerId>> votes;
    PlayerId executed = kNoTarget;  // kNoTarget, если голосов ни за кого не было
    bool was_tie = false;           // несколько кандидатов набрали поровну голосов
    PlayerId sergeant_promoted = kNoTarget;  // Сержант, ставший Комиссаром после казни Комиссара
};

// Итог одного полного раунда: День и, если игра после него продолжается, Ночь.
struct RoundReport {
    DayReport day;
    NightReport night;
    bool night_played = false;  // false, если игра уже завершилась днём
};

enum class GameResult {
    InProgress,
    TownWins,
    MafiaWins,
    ManiacWins,
};

// Чем Ведущий исполняет ходы игроков внутри одной фазы.
enum class ExecutionMode {
    Threads,     // по std::thread на игрока (базовый вариант, п.2)
    Coroutines,  // кооперативные корутины в одном потоке (п.4)
};

// Ведущий — точка синхронизации игры: раздаёт роли, хранит всех игроков,
// проводит дневное голосование и ночную фазу, проверяет условия победы.
class GameMaster {
public:
    // mafia_divisor — это k из формулы floor(N/k), k >= 3. Количество мафии
    // не может быть меньше 1, даже если N/k округляется в 0.
    explicit GameMaster(int player_count, int mafia_divisor = 3);

    // То же, но набор ролей берётся из конфигурации (см. role_config.hpp).
    // Бросает std::invalid_argument, если игроков слишком мало для выбранных ролей.
    GameMaster(int player_count, const RoleConfig& config);

    // Принимает уже готовый список игроков (роли заданы заранее вызывающим
    // кодом). Нужен в первую очередь тестам: позволяет собрать контролируемый
    // маленький состав ролей вместо случайного распределения на весь N.
    // Каждый игрок кладётся по индексу, равному его собственному id(), а не
    // позиции в списке — id должны быть различны и образовывать диапазон
    // [0; max_id] без пропусков.
    explicit GameMaster(std::vector<SharedPtr<Player>> players);

    const std::vector<SharedPtr<Player>>& all_players() const noexcept { return players_; }
    std::vector<SharedPtr<Player>> alive_players() const;

    int round_number() const noexcept { return round_number_; }

    void set_execution_mode(ExecutionMode mode) noexcept { mode_ = mode; }
    ExecutionMode execution_mode() const noexcept { return mode_; }

    // Канал консольного ввода/вывода для игроков-людей (по умолчанию
    // std::cin/std::cout). Тесты подменяют потоки через set_streams().
    InputBroker& input() noexcept { return input_; }

    // Увеличивает round_number(). По правилам День всегда идёт первым в
    // каждом цикле, поэтому именно здесь, а не в play_night(), стартует
    // новый номер раунда.
    DayReport play_day();

    // Использует текущий round_number() не увеличивая его: ночь относится
    // к тому же раунду, что и предшествующий ей день. Предполагается, что
    // play_day() для этого раунда уже был вызван.
    NightReport play_night();

    // Удобная обёртка: один полный цикл День -> (Ночь, если игра не
    // закончилась днём). Ночь пропускается, если play_day() уже определил
    // победителя.
    RoundReport play_round();

    GameResult check_winner() const;

private:
    static std::vector<SharedPtr<Player>> assign_roles(int player_count, const RoleConfig& config);

    // Если Комиссар погиб, а Сержант жив, заменяет Сержанта новым объектом
    // Commissar (то же id/имя/интерактивность, общее досье). Возвращает id
    // Сержанта или kNoTarget, если замены не было.
    PlayerId promote_sergeant_if_needed();

    std::vector<SharedPtr<Player>> players_;  // индекс вектора == PlayerId
    int round_number_ = 0;
    ExecutionMode mode_ = ExecutionMode::Threads;
    InputBroker input_;
};

}  // namespace mafia
