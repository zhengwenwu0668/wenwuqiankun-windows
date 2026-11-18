#include "domain/Constants.h"

#include <algorithm>

namespace fx::domain {

const std::array<std::string, 24> MOUNTAINS = {
    "子", "癸", "丑", "艮", "寅", "甲", "卯", "乙", "辰", "巽", "巳", "丙",
    "午", "丁", "未", "坤", "申", "庚", "酉", "辛", "戌", "乾", "亥", "壬",
};

const std::array<int, 24> MOUNTAIN_TO_PALACE = {
    7, 7, 6, 6, 6, 3, 3, 3, 0, 0, 0, 1,
    1, 1, 2, 2, 2, 5, 5, 5, 8, 8, 8, 7,
};

const std::array<int, 9> FLY_ORDER = {4, 8, 5, 6, 1, 7, 2, 3, 0};

const std::array<std::string, 10> TIAN_YUAN_LONG = {
    "", "子", "坤", "卯", "巽", "", "乾", "酉", "艮", "午",
};

const std::array<std::string, 10> DI_YUAN_LONG = {
    "", "壬", "未", "甲", "辰", "", "戌", "庚", "丑", "丙",
};

const std::array<std::string, 10> REN_YUAN_LONG = {
    "", "癸", "申", "乙", "巳", "", "亥", "辛", "寅", "丁",
};

const std::array<std::string, 9> YUN_LABELS = {
    "一", "二", "三", "四", "五", "六", "七", "八", "九",
};

const std::array<std::string, 9> YUN_YEARS = {
    "1864-1883", "1884-1903", "1904-1923", "1924-1943", "1944-1963",
    "1964-1983", "1984-2003", "2004-2023", "2024-2043",
};

// ZUO_XUANG_PAIRS[i] 描述 MOUNTAINS[i] 的坐向，i 即 24 山顺时针下标（子=0，0°）。
const std::array<std::string, 24> ZUO_XUANG_PAIRS = {
    "子山午向", "癸山丁向", "丑山未向", "艮山坤向", "寅山申向", "甲山庚向",
    "卯山酉向", "乙山辛向", "辰山戌向", "巽山乾向", "巳山亥向", "丙山壬向",
    "午山子向", "丁山癸向", "未山丑向", "坤山艮向", "申山寅向", "庚山甲向",
    "酉山卯向", "辛山乙向", "戌山辰向", "乾山巽向", "亥山巳向", "壬山丙向",
};

namespace {
const std::array<std::string, 12> YIN_MOUNTAINS = {
    "子", "午", "卯", "酉", "辰", "戌", "丑", "未", "癸", "丁", "乙", "辛",
};

struct TiGuaEntry {
    const char *ch;
    int value;
};

const std::array<TiGuaEntry, 24> TI_GUA = {
    TiGuaEntry{"子", 1},   TiGuaEntry{"癸", 1},   TiGuaEntry{"甲", 1},   TiGuaEntry{"申", 1},
    TiGuaEntry{"坤", 2},   TiGuaEntry{"壬", 2},   TiGuaEntry{"乙", 2},   TiGuaEntry{"卯", 2},
    TiGuaEntry{"未", 2},   TiGuaEntry{"乾", 6},   TiGuaEntry{"巽", 6},   TiGuaEntry{"亥", 6},
    TiGuaEntry{"巳", 6},   TiGuaEntry{"戌", 6},   TiGuaEntry{"辰", 6},   TiGuaEntry{"艮", 7},
    TiGuaEntry{"丙", 7},   TiGuaEntry{"辛", 7},   TiGuaEntry{"酉", 7},   TiGuaEntry{"丑", 7},
    TiGuaEntry{"寅", 9},   TiGuaEntry{"午", 9},   TiGuaEntry{"庚", 9},   TiGuaEntry{"丁", 9},
};
} // namespace

bool isYin(const std::string &mountainChar) {
    return std::find(YIN_MOUNTAINS.begin(), YIN_MOUNTAINS.end(), mountainChar) != YIN_MOUNTAINS.end();
}

int tiGua(const std::string &refChar, int fallback) {
    for (const auto &entry : TI_GUA) {
        if (refChar == entry.ch) {
            return entry.value;
        }
    }
    return fallback;
}

// 三元龙归属只由字符本身决定，与入中数无关。
Dragon dragonOf(const std::string &mountainChar) {
    if (std::find(TIAN_YUAN_LONG.begin(), TIAN_YUAN_LONG.end(), mountainChar) != TIAN_YUAN_LONG.end()) {
        return Dragon::Tian;
    }
    if (std::find(DI_YUAN_LONG.begin(), DI_YUAN_LONG.end(), mountainChar) != DI_YUAN_LONG.end()) {
        return Dragon::Di;
    }
    return Dragon::Ren;
}

std::string dragonChar(int luoshuNum, Dragon dragon) {
    if (luoshuNum < 0 || luoshuNum > 9) {
        return {};
    }
    switch (dragon) {
    case Dragon::Tian:
        return TIAN_YUAN_LONG[static_cast<std::size_t>(luoshuNum)];
    case Dragon::Di:
        return DI_YUAN_LONG[static_cast<std::size_t>(luoshuNum)];
    case Dragon::Ren:
        return REN_YUAN_LONG[static_cast<std::size_t>(luoshuNum)];
    }
    return {};
}

} // namespace fx::domain
