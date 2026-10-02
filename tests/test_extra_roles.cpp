#include "mafia/game_master.hpp"
#include "mafia/game_view.hpp"
#include "mafia/roles/civilian.hpp"
#include "mafia/roles/commissar.hpp"
#include "mafia/roles/mafia.hpp"
#include "mafia/roles/mafia_council.hpp"
#include "mafia/roles/resuscitator.hpp"
#include "mafia/roles/sergeant.hpp"
#include "mafia/roles/thief.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mafia;
using mafia::roles::Civilian;
using mafia::roles::Commissar;
using mafia::roles::CommissarIntel;
using mafia::roles::Mafia;
using mafia::roles::MafiaCouncil;
using mafia::roles::Resuscitator;
using mafia::roles::Sergeant;
using mafia::roles::Thief;

namespace {

// Тестовый дублёр: роль и ночное действие задаются заранее, голосует за
// заданного игрока. Позволяет проверять разрешение ночи Ведущим без случайности.
class Scripted : public Player {
public:
    Scripted(PlayerId id, Role role, NightAction action = {}, PlayerId vote_for = kNoTarget)
        : Player(id, "P" + std::to_string(id)), role_(role), action_(action), vote_for_(vote_for) {}

    Role role() const noexcept override { return role_; }
    PlayerId vote(const GameView&) override { return vote_for_; }
    NightAction act(const GameView&) override { return action_; }

private:
    Role role_;
    NightAction action_;
    PlayerId vote_for_;
};

SharedPtr<Player> scripted(PlayerId id, Role role, NightAction action = {}, PlayerId vote_for = kNoTarget) {
    return make_shared_ptr<Scripted>(id, role, action, vote_for);
}

// ---------- Вор ----------

NightReport night_with_thief(NightAction thief_action, Role blocked_role, NightAction blocked_action) {
    std::vector<SharedPtr<Player>> players = {
        scripted(0, Role::Mafia, {ActionType::Kill, 2}),
        scripted(1, Role::Thief, thief_action),
        scripted(2, blocked_role, blocked_action),
        scripted(3, Role::Civilian),
    };
    GameMaster gm(std::move(players));
    return gm.play_night();
}

void test_thief_block_cancels_doctor_heal() {
    // Без Вора Доктор вылечил бы себя и спас от мафии.
    NightReport saved = night_with_thief({}, Role::Doctor, {ActionType::Heal, 2});
    assert(saved.killed.empty());

    // С Вором лечение не срабатывает, и мафия убивает Доктора.
    NightReport blocked = night_with_thief({ActionType::Block, 2}, Role::Doctor, {ActionType::Heal, 2});
    assert(blocked.block_effective);
    assert(blocked.block_target == 2);
    assert((blocked.killed == std::vector<PlayerId>{2}));
    assert(blocked.healed == kNoTarget);
}

void test_thief_block_cancels_commissar_check() {
    NightReport report = night_with_thief({ActionType::Block, 2}, Role::Commissar, {ActionType::Check, 0});
    assert(report.block_effective);
    assert(report.commissar_check_target == kNoTarget);  // проверка не состоялась
}

void test_thief_block_has_no_effect_on_non_town_roles() {
    // Блокируют Маньяка: у него нет способностей мирной стороны, его выстрел остаётся.
    std::vector<SharedPtr<Player>> players = {
        scripted(0, Role::Thief, {ActionType::Block, 1}),
        scripted(1, Role::Maniac, {ActionType::Kill, 2}),
        scripted(2, Role::Civilian),
    };
    // Вор единственный в банде -> он Босс и действовал бы как стрелок, но здесь
    // он сыгран дублёром, поэтому просто проверяем разрешение блокировки.
    GameMaster gm(std::move(players));
    NightReport report = gm.play_night();
    assert(!report.block_effective);
    assert((report.killed == std::vector<PlayerId>{2}));
}

void test_thief_block_on_civilian_is_ineffective() {
    NightReport report = night_with_thief({ActionType::Block, 3}, Role::Doctor, {ActionType::Heal, 2});
    assert(!report.block_effective);  // 3 — обычный мирный, блокировать нечего
    assert(report.killed.empty());    // лечение Доктора сработало
}

void test_thief_bot_blocks_outsiders_and_shoots_when_last_in_gang() {
    auto council = make_shared_ptr<MafiaCouncil>();
    council->members = {0, 1};
    council->thieves = {1};
    auto boss = make_shared_ptr<Mafia>(0, "Boss", council);
    auto thief = make_shared_ptr<Thief>(1, "Thief", council);
    auto town = make_shared_ptr<Civilian>(2, "Town");

    GameView view;
    view.alive_players = {boss, thief, town};

    assert(council->current_boss(view) == 0);  // Боссом остаётся обычный мафиози
    for (int i = 0; i < 30; ++i) {
        NightAction action = thief->act(view);
        assert(action.type == ActionType::Block);
        assert(action.target == 2);  // своих (банду) не блокирует
    }
    assert(boss->act(view).type == ActionType::Kill);

    // Обычный мафиози погиб: Вор остался один и стреляет сам.
    view.alive_players = {thief, town};
    assert(council->current_boss(view) == 1);
    NightAction fallback = thief->act(view);
    assert(fallback.type == ActionType::Kill);
    assert(fallback.target == 2);
}

// ---------- Реаниматор ----------

void test_resuscitator_acts_last_others_do_not() {
    assert(Resuscitator(0, "R").acts_after_resolution());
    assert(!Civilian(0, "C").acts_after_resolution());
    assert(!Commissar(0, "C").acts_after_resolution());
}

void test_resuscitator_bot_revives_only_tonights_victims_and_never_twice() {
    auto reviver = make_shared_ptr<Resuscitator>(0, "Reviver");
    auto victim_a = make_shared_ptr<Civilian>(1, "A");
    auto victim_b = make_shared_ptr<Civilian>(2, "B");

    GameView view;
    view.alive_players = {reviver};
    view.killed_tonight = {victim_a, victim_b};

    NightAction first = reviver->act(view);
    assert(first.type == ActionType::Resurrect);
    assert(first.target == 1 || first.target == 2);

    reviver->note_revived(first.target);
    NightAction second = reviver->act(view);
    assert(second.type == ActionType::Resurrect);
    assert(second.target != first.target);  // одного и того же дважды нельзя

    reviver->note_revived(second.target);
    assert(reviver->act(view).type == ActionType::None);  // воскрешать больше некого
}

void test_resuscitator_does_nothing_when_nobody_died_tonight() {
    auto reviver = make_shared_ptr<Resuscitator>(0, "Reviver");
    GameView view;
    view.alive_players = {reviver};
    assert(reviver->act(view).type == ActionType::None);
}

// Реаниматор видит и воскрешает только погибших ЭТОЙ ночью; казнённый
// голосованием (раньше) ему недоступен.
void test_gamemaster_offers_only_tonights_victims_not_executed_players() {
    auto executed = make_shared_ptr<Civilian>(3, "Executed");
    executed->kill();  // выбыл днём по итогам голосования
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Resuscitator>(0, "Reviver"),
        make_shared_ptr<Civilian>(1, "Victim"),
        scripted(2, Role::Maniac, {ActionType::Kill, 1}),
        executed,
    };
    GameMaster gm(std::move(players));

    NightReport report = gm.play_night();
    assert((report.killed == std::vector<PlayerId>{1}));
    assert(report.resurrected == 1);  // единственный кандидат — жертва этой ночи
    assert(gm.all_players()[1]->is_alive());
    assert(!gm.all_players()[3]->is_alive());  // казнённого не тронули
}

void test_no_resurrection_when_only_executed_players_are_dead() {
    auto executed = make_shared_ptr<Civilian>(1, "Executed");
    executed->kill();
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Resuscitator>(0, "Reviver"), executed, scripted(2, Role::Civilian)};
    GameMaster gm(std::move(players));

    NightReport report = gm.play_night();
    assert(report.killed.empty());
    assert(report.resurrected == kNoTarget);
    assert(!gm.all_players()[1]->is_alive());
}

void test_victim_saved_by_doctor_is_not_offered_to_resuscitator() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Resuscitator>(0, "Reviver"),
        make_shared_ptr<Civilian>(1, "Victim"),
        scripted(2, Role::Maniac, {ActionType::Kill, 1}),
        scripted(3, Role::Doctor, {ActionType::Heal, 1}),
    };
    GameMaster gm(std::move(players));

    NightReport report = gm.play_night();
    assert(report.killed.empty());  // "собирается умирать" считается уже после лечения
    assert(report.resurrected == kNoTarget);
}

void test_revival_cancels_the_death_but_only_once_per_player() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Resuscitator>(0, "Reviver"),
        make_shared_ptr<Civilian>(1, "Victim"),
        scripted(2, Role::Maniac, {ActionType::Kill, 1}),
    };
    GameMaster gm(std::move(players));

    NightReport first = gm.play_night();
    assert(first.resurrected == 1);
    assert(gm.all_players()[1]->is_alive());

    NightReport second = gm.play_night();  // Маньяк снова убивает того же
    assert((second.killed == std::vector<PlayerId>{1}));
    assert(second.resurrected == kNoTarget);  // дважды одного и того же нельзя
    assert(!gm.all_players()[1]->is_alive());
}

void test_blocked_resuscitator_cannot_revive() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Resuscitator>(0, "Reviver"),
        make_shared_ptr<Civilian>(1, "Victim"),
        scripted(2, Role::Maniac, {ActionType::Kill, 1}),
        scripted(3, Role::Thief, {ActionType::Block, 0}),
    };
    GameMaster gm(std::move(players));

    NightReport report = gm.play_night();
    assert(report.block_effective);
    assert(report.resurrected == kNoTarget);
    assert(!gm.all_players()[1]->is_alive());
}

void test_resuscitator_killed_tonight_may_still_revive_himself() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Resuscitator>(0, "Reviver"),
        scripted(1, Role::Maniac, {ActionType::Kill, 0}),
        scripted(2, Role::Civilian),
    };
    GameMaster gm(std::move(players));

    NightReport report = gm.play_night();
    assert((report.killed == std::vector<PlayerId>{0}));
    assert(report.resurrected == 0);  // ходит последним и успевает назвать себя
    assert(gm.all_players()[0]->is_alive());
}

void test_revived_commissar_is_not_replaced_by_sergeant() {
    auto intel = make_shared_ptr<CommissarIntel>();
    intel->commissar_id = 0;
    intel->sergeant_id = 1;
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Commissar>(0, "Com", intel),
        make_shared_ptr<Sergeant>(1, "Sgt", intel),
        make_shared_ptr<Resuscitator>(2, "Reviver"),
        scripted(3, Role::Maniac, {ActionType::Kill, 0}),
    };
    GameMaster gm(std::move(players));

    NightReport report = gm.play_night();
    assert(report.resurrected == 0);
    assert(report.sergeant_promoted == kNoTarget);  // Комиссар жив, замены нет
    assert(gm.all_players()[1]->role() == Role::Sergeant);
}

void test_interactive_resuscitator_chooses_or_skips_in_both_modes() {
    for (ExecutionMode mode : {ExecutionMode::Threads, ExecutionMode::Coroutines}) {
        for (bool skip : {true, false}) {
            auto reviver = make_shared_ptr<Resuscitator>(0, "Reviver");
            reviver->set_interactive();
            std::vector<SharedPtr<Player>> players = {
                reviver, make_shared_ptr<Civilian>(1, "Victim"), scripted(2, Role::Maniac, {ActionType::Kill, 1})};
            GameMaster gm(std::move(players));
            gm.set_execution_mode(mode);
            std::istringstream in(skip ? "-\n" : "1\n");
            std::ostringstream out;
            gm.input().set_streams(in, out);

            NightReport report = gm.play_night();
            assert(out.str().find("погибшие этой ночью") != std::string::npos);
            assert((report.resurrected == 1) == !skip);
            assert(gm.all_players()[1]->is_alive() == !skip);
        }
    }
}

// ---------- Сержант ----------

void test_sergeant_bot_votes_against_known_alive_mafia_only() {
    auto intel = make_shared_ptr<CommissarIntel>();
    intel->checks = {{3, true}, {2, false}};
    auto sergeant = make_shared_ptr<Sergeant>(0, "Sgt", intel);
    auto a = make_shared_ptr<Civilian>(2, "A");
    auto mafia_suspect = make_shared_ptr<Civilian>(3, "M");

    GameView view;
    view.alive_players = {sergeant, a, mafia_suspect};
    for (int i = 0; i < 30; ++i) {
        assert(sergeant->vote(view) == 3);
    }
    assert(sergeant->act(view).type == ActionType::None);  // сам не проверяет

    view.alive_players = {sergeant, a};  // изобличённый уже мёртв — голосует как все
    for (int i = 0; i < 30; ++i) {
        assert(sergeant->vote(view) == 2);  // единственный кандидат, кроме себя
    }
}

void test_sergeant_replaces_executed_commissar_and_inherits_dossier() {
    auto intel = make_shared_ptr<CommissarIntel>();
    auto commissar = make_shared_ptr<Commissar>(0, "Com", intel);
    auto sergeant = make_shared_ptr<Sergeant>(1, "Sgt", intel);
    intel->commissar_id = 0;
    intel->sergeant_id = 1;
    commissar->record_check(3, true);  // Комиссар уже изобличил игрока 3
    sergeant->set_interactive();

    std::vector<SharedPtr<Player>> players = {
        commissar,
        sergeant,
        scripted(2, Role::Civilian, {}, 0),
        scripted(3, Role::Mafia, {}, 0),
        scripted(4, Role::Civilian, {}, 0),
    };
    GameMaster gm(std::move(players));
    std::istringstream in("0\n");  // человек-Сержант голосует против Комиссара: большинство гарантировано
    std::ostringstream out;
    gm.input().set_streams(in, out);

    DayReport day = gm.play_day();
    assert(day.executed == 0);
    assert(day.sergeant_promoted == 1);

    const SharedPtr<Player>& promoted = gm.all_players()[1];
    assert(promoted->role() == Role::Commissar);
    assert(promoted->is_alive());
    assert(promoted->name() == "Sgt");
    assert(promoted->is_interactive());  // человек остаётся человеком

    auto* new_commissar = dynamic_cast<Commissar*>(promoted.get());
    assert(new_commissar != nullptr);
    assert(new_commissar->intel()->commissar_id == 1);
    assert(new_commissar->intel()->sergeant_id == kNoTarget);
    assert(new_commissar->intel()->checks.size() == 1);  // история проверок унаследована
}

void test_bot_commissar_shoots_known_mafia() {
    auto intel = make_shared_ptr<CommissarIntel>();
    auto commissar = make_shared_ptr<Commissar>(0, "Com", intel);
    auto suspect = make_shared_ptr<Civilian>(3, "M");
    auto other = make_shared_ptr<Civilian>(4, "O");
    commissar->record_check(3, true);

    GameView view;
    view.alive_players = {commissar, suspect, other};
    NightAction action = commissar->act(view);
    assert(action.type == ActionType::Shoot);
    assert(action.target == 3);
    assert(commissar->vote(view) == 3);
}

void test_sergeant_replaces_commissar_killed_at_night() {
    auto intel = make_shared_ptr<CommissarIntel>();
    intel->commissar_id = 0;
    intel->sergeant_id = 1;
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Commissar>(0, "Com", intel),
        make_shared_ptr<Sergeant>(1, "Sgt", intel),
        scripted(2, Role::Maniac, {ActionType::Kill, 0}),
    };
    GameMaster gm(std::move(players));

    NightReport report = gm.play_night();
    assert((report.killed == std::vector<PlayerId>{0}));
    assert(report.sergeant_promoted == 1);
    assert(gm.all_players()[1]->role() == Role::Commissar);

    // Больше замены не будет: Сержанта нет.
    NightReport next = gm.play_night();
    assert(next.sergeant_promoted == kNoTarget);
}

void test_commissar_check_result_recorded_in_dossier_and_thief_counts_as_mafia() {
    auto intel = make_shared_ptr<CommissarIntel>();
    intel->commissar_id = 0;
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Commissar>(0, "Com", intel),
        scripted(1, Role::Thief),
    };
    GameMaster gm(std::move(players));

    NightReport report = gm.play_night();  // единственный кандидат — Вор
    assert(report.commissar_check_target == 1);
    assert(report.commissar_check_result_mafia);  // Вор — лагерь мафии
    assert(intel->checks.size() == 1);
    assert(intel->checks[0].target == 1 && intel->checks[0].is_mafia);
}

// ---------- Комиссар и Сержант не предлагают друг друга ----------

void test_commissar_bot_never_targets_his_sergeant() {
    auto intel = make_shared_ptr<CommissarIntel>();
    intel->commissar_id = 0;
    intel->sergeant_id = 1;
    auto commissar = make_shared_ptr<Commissar>(0, "Com", intel);
    auto sergeant = make_shared_ptr<Sergeant>(1, "Sgt", intel);
    auto other = make_shared_ptr<Civilian>(2, "Other");

    GameView view;
    view.alive_players = {commissar, sergeant, other};
    for (int i = 0; i < 40; ++i) {
        NightAction action = commissar->act(view);
        assert(action.type == ActionType::Check);
        assert(action.target == 2);        // единственный кандидат, кроме себя и Сержанта
        assert(commissar->vote(view) == 2);
    }
    // Даже перепроверяя (все уже проверены) Сержанта он не выберет.
    for (int i = 0; i < 40; ++i) {
        assert(commissar->act(view).target == 2);
    }
}

void test_sergeant_bot_never_votes_for_his_commissar() {
    auto intel = make_shared_ptr<CommissarIntel>();
    intel->commissar_id = 0;
    intel->sergeant_id = 1;
    auto commissar = make_shared_ptr<Commissar>(0, "Com", intel);
    auto sergeant = make_shared_ptr<Sergeant>(1, "Sgt", intel);
    auto other = make_shared_ptr<Civilian>(2, "Other");

    GameView view;
    view.alive_players = {commissar, sergeant, other};
    for (int i = 0; i < 40; ++i) {
        assert(sergeant->vote(view) == 2);
    }
}

void test_interactive_lists_do_not_offer_the_partner() {
    auto intel = make_shared_ptr<CommissarIntel>();
    intel->commissar_id = 0;
    intel->sergeant_id = 1;
    auto commissar = make_shared_ptr<Commissar>(0, "Com", intel);
    auto sergeant = make_shared_ptr<Sergeant>(1, "Sgt", intel);
    auto other = make_shared_ptr<Civilian>(2, "Other");
    commissar->set_interactive();
    sergeant->set_interactive();

    GameView view;
    view.alive_players = {commissar, sergeant, other};

    // Комиссар: голосование и проверка. Ответ "1" (Сержант) отклоняется.
    {
        std::istringstream in("1\n2\ncheck\n1\n2\n");
        std::ostringstream out;
        InputBroker broker(in, out);
        view.input = &broker;

        assert(commissar->vote(view) == 2);
        assert(commissar->act(view).target == 2);
        std::string text = out.str();
        assert(text.find("  1: Sgt") == std::string::npos);  // Сержанта нет в списке
        assert(text.find("  2: Other") != std::string::npos);
        assert(text.find("Такого игрока нет среди доступных целей") != std::string::npos);
    }
    // Сержант: Комиссара в списке нет.
    {
        std::istringstream in("0\n2\n");
        std::ostringstream out;
        InputBroker broker(in, out);
        view.input = &broker;

        assert(sergeant->vote(view) == 2);
        assert(out.str().find("  0: Com") == std::string::npos);
        assert(out.str().find("Такого игрока нет среди доступных целей") != std::string::npos);
    }
}

// ---------- Раздача по конфигурации ----------

RoleConfig all_extras() {
    RoleConfig config;
    config.sergeant = config.resuscitator = config.thief = true;
    return config;
}

void test_assign_roles_with_all_extra_roles() {
    GameMaster gm(12, all_extras());  // floor(12/3) = 4 места мафии, одно из них — Вор

    std::map<Role, int> count;
    for (const auto& player : gm.all_players()) {
        ++count[player->role()];
    }
    assert(count[Role::Mafia] == 3);
    assert(count[Role::Thief] == 1);  // Вор занимает место мафии, а не добавляется сверх
    assert(count[Role::Doctor] == 1);
    assert(count[Role::Commissar] == 1);
    assert(count[Role::Maniac] == 1);
    assert(count[Role::Sergeant] == 1);
    assert(count[Role::Resuscitator] == 1);
    assert(count[Role::Civilian] == 12 - 4 - 5);

    // Сержант и Комиссар разделяют одно досье с правильными id.
    const Sergeant* sergeant = nullptr;
    PlayerId commissar_id = kNoTarget;
    for (const auto& player : gm.all_players()) {
        if (auto* s = dynamic_cast<const Sergeant*>(player.get())) {
            sergeant = s;
        }
        if (player->role() == Role::Commissar) {
            commissar_id = player->id();
        }
    }
    assert(sergeant != nullptr);
    assert(sergeant->intel()->commissar_id == commissar_id);
    assert(sergeant->intel()->sergeant_id == sergeant->id());
}

void test_assign_roles_without_optional_roles() {
    RoleConfig config;
    config.maniac = false;
    config.doctor = false;
    GameMaster gm(6, config);
    for (const auto& player : gm.all_players()) {
        assert(player->role() != Role::Maniac && player->role() != Role::Doctor);
    }
}

void test_too_few_players_for_roles_throws() {
    bool threw = false;
    try {
        GameMaster gm(5, all_extras());  // 1 мафия + 5 особых ролей > 5 игроков
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    GameMaster ok(5);  // набор по умолчанию на 5 игроков помещается
    assert(ok.all_players().size() == 5);
}

void test_full_games_with_all_roles_terminate_in_both_modes() {
    for (ExecutionMode mode : {ExecutionMode::Threads, ExecutionMode::Coroutines}) {
        for (int trial = 0; trial < 40; ++trial) {
            GameMaster gm(8 + trial % 13, all_extras());
            gm.set_execution_mode(mode);
            int rounds = 0;
            while (gm.check_winner() == GameResult::InProgress) {
                gm.play_round();
                assert(++rounds < 200);
            }
        }
    }
}

}  // namespace

int main() {
    test_thief_block_cancels_doctor_heal();
    test_thief_block_cancels_commissar_check();
    test_thief_block_has_no_effect_on_non_town_roles();
    test_thief_block_on_civilian_is_ineffective();
    test_thief_bot_blocks_outsiders_and_shoots_when_last_in_gang();
    test_resuscitator_acts_last_others_do_not();
    test_resuscitator_bot_revives_only_tonights_victims_and_never_twice();
    test_resuscitator_does_nothing_when_nobody_died_tonight();
    test_gamemaster_offers_only_tonights_victims_not_executed_players();
    test_no_resurrection_when_only_executed_players_are_dead();
    test_victim_saved_by_doctor_is_not_offered_to_resuscitator();
    test_revival_cancels_the_death_but_only_once_per_player();
    test_blocked_resuscitator_cannot_revive();
    test_resuscitator_killed_tonight_may_still_revive_himself();
    test_revived_commissar_is_not_replaced_by_sergeant();
    test_interactive_resuscitator_chooses_or_skips_in_both_modes();
    test_sergeant_bot_votes_against_known_alive_mafia_only();
    test_sergeant_replaces_executed_commissar_and_inherits_dossier();
    test_bot_commissar_shoots_known_mafia();
    test_sergeant_replaces_commissar_killed_at_night();
    test_commissar_check_result_recorded_in_dossier_and_thief_counts_as_mafia();
    test_commissar_bot_never_targets_his_sergeant();
    test_sergeant_bot_never_votes_for_his_commissar();
    test_interactive_lists_do_not_offer_the_partner();
    test_assign_roles_with_all_extra_roles();
    test_assign_roles_without_optional_roles();
    test_too_few_players_for_roles_throws();
    test_full_games_with_all_roles_terminate_in_both_modes();
    std::cout << "All extra-role tests passed.\n";
    return 0;
}
