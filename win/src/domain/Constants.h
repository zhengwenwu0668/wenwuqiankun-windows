#pragma once

#include <array>
#include <string>

// 领域常量：逐字对应 Android 版 domain/Constants.kt，不得改动。
namespace fx::domain {

enum class Dragon {
    Tian,
    Di,
    Ren,
};

extern const std::array<std::string, 24> MOUNTAINS;
extern const std::array<int, 24> MOUNTAIN_TO_PALACE;
extern const std::array<int, 9> FLY_ORDER;

// 三元龙：下标为洛书数 1..9，0 与 5 无字（与 Kotlin 的 map 缺键等价）。
extern const std::array<std::string, 10> TIAN_YUAN_LONG;
extern const std::array<std::string, 10> DI_YUAN_LONG;
extern const std::array<std::string, 10> REN_YUAN_LONG;

extern const std::array<std::string, 9> YUN_LABELS;
extern const std::array<std::string, 9> YUN_YEARS;
extern const std::array<std::string, 24> ZUO_XUANG_PAIRS;

bool isYin(const std::string &mountainChar);

// 替卦表查 refChar，未收录时返回 fallback（对应 Kotlin 的 TI_GUA[ch] ?: originCenter）。
int tiGua(const std::string &refChar, int fallback);

Dragon dragonOf(const std::string &mountainChar);
std::string dragonChar(int luoshuNum, Dragon dragon);

} // namespace fx::domain
