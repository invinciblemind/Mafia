#pragma once

#include <cstddef>
#include <limits>

namespace mafia {

// Идентификатор игрока — просто номер (индекс), а не указатель/SharedPtr.
// Игроки не хранят ссылки друг на друга: они лишь называют Ведущему номер
// того, на кого направлено действие.
using PlayerId = std::size_t;

// "Нет цели" — используется, когда действие пропущено или недоступно.
inline constexpr PlayerId kNoTarget = std::numeric_limits<PlayerId>::max();

// Тип ночного действия. Разные роли кладут в него разный смысл:
//   Doctor    -> Heal  (лечить target)
//   Commissar -> Check (проверить статус target) или Shoot (застрелить target)
//   Mafia     -> Kill  (убить target)
//   Maniac    -> Kill  (убить target)
//   Thief     -> Block (заблокировать target) или Kill, если он последний в банде
//   Resuscitator -> Resurrect (воскресить погибшего target)
//   Civilian  -> None  (нет ночных действий)
enum class ActionType {
    None,
    Heal,
    Check,
    Shoot,
    Kill,
    Block,      // Вор: заблокировать ночное действие особой роли мирного
    Resurrect,  // Реаниматор: воскресить погибшего игрока
};

// Решение игрока по итогам ночного хода. Игрок только формирует это решение
// и возвращает его из act() — применяет его (лечит/убивает/проверяет)
// центральный резолвер ночи у Ведущего, а не сам игрок.
struct NightAction {
    ActionType type = ActionType::None;
    PlayerId target = kNoTarget;
};

}  // namespace mafia
