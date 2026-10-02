#pragma once

#include <vector>

#include "mafia/action.hpp"
#include "mafia/player.hpp"
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
};

// Ведущий — точка синхронизации игры: раздаёт роли, хранит всех игроков и
// разрешает ночные действия. Дневное голосование и проверка условий победы
// добавятся следующим шагом.
class GameMaster {
public:
    // mafia_divisor — это k из формулы floor(N/k), k >= 3. Количество мафии
    // не может быть меньше 1, даже если N/k округляется в 0.
    explicit GameMaster(int player_count, int mafia_divisor = 3);

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

    NightReport play_night();

private:
    static std::vector<SharedPtr<Player>> assign_roles(int player_count, int mafia_divisor);

    std::vector<SharedPtr<Player>> players_;  // индекс вектора == PlayerId
    int round_number_ = 0;
};

}  // namespace mafia
