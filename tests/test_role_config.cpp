#include "mafia/role_config.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using mafia::ConfigError;
using mafia::parse_role_config;
using mafia::RoleConfig;

namespace fs = std::filesystem;

namespace {

// Разбор должен упасть с ConfigError, сообщение которого содержит все подстроки.
template <typename... Needles>
void expect_error(const std::string& yaml, const Needles&... needles) {
    bool threw = false;
    try {
        parse_role_config(yaml);
    } catch (const ConfigError& error) {
        threw = true;
        std::string message = error.what();
        ((assert(message.find(needles) != std::string::npos)), ...);
    }
    assert(threw);
}

void test_default_config_has_the_mandatory_roles() {
    RoleConfig config = mafia::default_role_config();
    assert(config.mafia_divisor == 3);
    assert(config.doctor && config.commissar && config.maniac);
    assert(!config.sergeant && !config.resuscitator && !config.thief);
}

void test_block_list_with_comments() {
    RoleConfig config = parse_role_config(
        "# набор ролей\n"
        "mafia_divisor: 4   # чаще мирные\n"
        "roles:\n"
        "  - doctor\n"
        "  - sergeant   # помощник\n"
        "  # закомментированный пункт\n"
        "  - thief\n");
    assert(config.mafia_divisor == 4);
    assert(config.doctor && config.sergeant && config.thief);
    assert(!config.commissar && !config.maniac && !config.resuscitator);  // не перечислены -> выключены
}

void test_list_at_key_indentation_and_inline_list() {
    RoleConfig block = parse_role_config("roles:\n- commissar\n- resuscitator\n");
    assert(block.commissar && block.resuscitator && !block.doctor);

    RoleConfig inline_list = parse_role_config("roles: [maniac, \"thief\", 'sergeant']\n");
    assert(inline_list.maniac && inline_list.thief && inline_list.sergeant && !inline_list.doctor);
}

void test_names_are_case_insensitive_and_may_be_russian() {
    RoleConfig config = parse_role_config("roles:\n  - Доктор\n  - КОМИССАР\n  - Реаниматор\n  - Вор\n  - Sergeant\n");
    assert(config.doctor && config.commissar && config.resuscitator && config.thief && config.sergeant);
    assert(!config.maniac);
}

void test_mafia_and_civilian_may_be_listed_but_are_not_required() {
    RoleConfig config = parse_role_config("roles:\n  - mafia\n  - мирный\n  - civilian\n  - doctor\n");
    assert(config.doctor && !config.commissar);
}

void test_missing_roles_key_keeps_defaults_and_empty_list_disables_all() {
    RoleConfig only_divisor = parse_role_config("mafia_divisor: 5\n");
    assert(only_divisor.mafia_divisor == 5);
    assert(only_divisor.doctor && only_divisor.commissar && only_divisor.maniac);

    RoleConfig none = parse_role_config("roles: []\n");
    assert(!none.doctor && !none.commissar && !none.maniac && !none.sergeant && !none.resuscitator && !none.thief);

    RoleConfig empty_document = parse_role_config("# только комментарий\n\n");
    assert(empty_document.doctor);
}

void test_errors_name_the_line() {
    expect_error("roles:\n  - doctor\n  - wizard\n", "неизвестная роль", "wizard");
    expect_error("mafia_divisor: 2\n", "строка 1", "не меньше 3");
    expect_error("mafia_divisor: много\n", "строка 1", "целым числом");
    expect_error("flavour: mint\n", "строка 1", "неизвестный ключ");
    expect_error("roles:\n  - doctor\nroles:\n  - maniac\n", "строка 3", "дважды");
    expect_error("roles:\n  - doctor\n  - doctor\n", "роль", "дважды");
    expect_error("- doctor\n", "строка 1", "без ключа");
    expect_error("roles: doctor\n", "roles должен быть списком");
    expect_error("roles:\n\t- doctor\n", "строка 2", "табуляция");
    expect_error("just words\n", "строка 1", "ключ: значение");
    expect_error("roles: [doctor, maniac\n", "']'");
    expect_error("roles:\n  nested: 1\n", "строка 2", "отступ");
}

void test_load_from_file_and_missing_file() {
    fs::path file = fs::temp_directory_path() /
                    ("mafia_roles_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml");
    {
        std::ofstream out(file);
        out << "roles:\n  - thief\n";
    }
    RoleConfig config = mafia::load_role_config(file);
    assert(config.thief && !config.doctor);

    {
        std::ofstream out(file);
        out << "roles:\n  - wizard\n";
    }
    bool threw = false;
    try {
        mafia::load_role_config(file);
    } catch (const ConfigError& error) {
        threw = true;
        std::string message = error.what();
        assert(message.find(file.string()) != std::string::npos);  // в ошибке есть имя файла
        assert(message.find("wizard") != std::string::npos);
    }
    assert(threw);
    fs::remove(file);

    threw = false;
    try {
        mafia::load_role_config(file);
    } catch (const ConfigError& error) {
        threw = std::string(error.what()).find("не удалось открыть") != std::string::npos;
    }
    assert(threw);
}

void test_example_configs_from_repository_parse() {
    RoleConfig basic = mafia::load_role_config(fs::path(MAFIA_SOURCE_DIR) / "config" / "roles_basic.yaml");
    assert(basic.doctor && basic.commissar && basic.maniac && !basic.thief);

    RoleConfig extended = mafia::load_role_config(fs::path(MAFIA_SOURCE_DIR) / "config" / "roles_extended.yaml");
    assert(extended.sergeant && extended.resuscitator && extended.thief);
}

void test_warnings() {
    RoleConfig fine;
    assert(mafia::role_config_warnings(fine).empty());

    RoleConfig lonely_reviver;
    lonely_reviver.maniac = lonely_reviver.commissar = false;
    lonely_reviver.resuscitator = true;
    assert(mafia::role_config_warnings(lonely_reviver).size() == 1);

    RoleConfig sergeant_alone;
    sergeant_alone.commissar = false;
    sergeant_alone.sergeant = true;
    assert(mafia::role_config_warnings(sergeant_alone).size() == 1);
}

}  // namespace

int main() {
    test_default_config_has_the_mandatory_roles();
    test_block_list_with_comments();
    test_list_at_key_indentation_and_inline_list();
    test_names_are_case_insensitive_and_may_be_russian();
    test_mafia_and_civilian_may_be_listed_but_are_not_required();
    test_missing_roles_key_keeps_defaults_and_empty_list_disables_all();
    test_errors_name_the_line();
    test_load_from_file_and_missing_file();
    test_example_configs_from_repository_parse();
    test_warnings();
    std::cout << "All role-config tests passed.\n";
    return 0;
}
