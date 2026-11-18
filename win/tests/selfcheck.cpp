// 验收门禁：安卓版规格文档（Agent.md）4.9 五组排盘数组 + 5.5 流年/流月星数，逐项精确比对。
// 不链接 Qt，纯控制台，便于 CI/脚本直接判定。

#include <cstdio>
#include <array>
#include <initializer_list>
#include <string>
#include <vector>

#include "domain/Constants.h"
#include "domain/FeixingCalculator.h"
#include "domain/YearMonthCalculator.h"

using namespace fx::domain;

namespace {

int failures = 0;

std::string toString(const std::array<int, 9> &pan) {
    std::string text = "[";
    for (std::size_t i = 0; i < pan.size(); ++i) {
        if (i > 0) {
            text += ",";
        }
        text += std::to_string(pan[i]);
    }
    text += "]";
    return text;
}

std::array<int, 9> toArray(std::initializer_list<int> values) {
    std::array<int, 9> pan{};
    std::size_t i = 0;
    for (const int value : values) {
        if (i < pan.size()) {
            pan[i++] = value;
        }
    }
    return pan;
}

bool equalPan(const std::array<int, 9> &actual, std::initializer_list<int> expected) {
    return expected.size() == 9 && actual == toArray(expected);
}

void checkPan(const char *label, const std::array<int, 9> &actual, std::initializer_list<int> expected) {
    if (expected.size() != 9) {
        ++failures;
        std::printf("  FAIL  %s expectation has %zu values, want 9\n", label, expected.size());
        return;
    }
    const std::array<int, 9> want = toArray(expected);
    if (actual == want) {
        std::printf("  PASS  %s = %s\n", label, toString(actual).c_str());
    } else {
        ++failures;
        std::printf("  FAIL  %s = %s, want %s\n", label, toString(actual).c_str(), toString(want).c_str());
    }
}

void checkTrue(const char *label, bool condition) {
    if (condition) {
        std::printf("  PASS  %s\n", label);
    } else {
        ++failures;
        std::printf("  FAIL  %s\n", label);
    }
}

struct Case {
    const char *name;
    int yun;
    float deg;
    std::initializer_list<int> yunPan;
    std::initializer_list<int> shanPan;
    std::initializer_list<int> xiangPan;
};

} // namespace

int main() {
    // 4.9 验收用例，数组顺序 [巽,离,坤, 震,中,兑, 艮,坎,乾]。
    const std::vector<Case> cases = {
        {"9yun zi-shan wu-xiang forward", 9, 0.0f, {8, 4, 6, 7, 9, 2, 3, 5, 1}, {6, 1, 8, 7, 5, 3, 2, 9, 4}, {3, 8, 1, 2, 4, 6, 7, 9, 5}},
        {"8yun zi-shan wu-xiang jian gui-ding", 8, 6.0f, {7, 3, 5, 6, 8, 1, 2, 4, 9}, {5, 1, 3, 4, 6, 8, 9, 2, 7}, {3, 7, 5, 4, 2, 9, 8, 6, 1}},
        {"8yun ren-shan bing-xiang forward", 8, 345.0f, {7, 3, 5, 6, 8, 1, 2, 4, 9}, {5, 9, 7, 6, 4, 2, 1, 8, 3}, {2, 7, 9, 1, 3, 5, 6, 8, 4}},
        {"9yun ren-shan bing-xiang forward", 9, 345.0f, {8, 4, 6, 7, 9, 2, 3, 5, 1}, {4, 9, 2, 3, 5, 7, 8, 1, 6}, {5, 9, 7, 6, 4, 2, 1, 8, 3}},
        {"9yun zi-shan wu-xiang jian gui-ding", 9, 6.0f, {8, 4, 6, 7, 9, 2, 3, 5, 1}, {6, 1, 8, 7, 5, 3, 2, 9, 4}, {5, 1, 3, 4, 6, 8, 9, 2, 7}},
    };

    std::printf("== feixing acceptance (5 cases) ==\n");
    for (const Case &c : cases) {
        std::printf("%s: yun=%d deg=%.1f\n", c.name, c.yun, static_cast<double>(c.deg));
        const FeixingResult r = computeFeixing(c.yun, c.deg);
        checkPan("yunPan", r.yunPan, c.yunPan);
        checkPan("shanPan", r.shanPan, c.shanPan);
        checkPan("xiangPan", r.xiangPan, c.xiangPan);
    }

    std::printf("== year/month stars (5.5) ==\n");
    // 中宫（下标 4）即入中数，可反推年/月星。
    checkTrue("2015-01 yearStar=4 (starYear 2014)", addYearMonth(2015, 1).yearPan[4] == 4);
    checkTrue("2015-02 yearStar=3", addYearMonth(2015, 2).yearPan[4] == 3);
    checkTrue("2020-06 yearStar=7", addYearMonth(2020, 6).yearPan[4] == 7);
    checkTrue("2020-06 monthStar=4", addYearMonth(2020, 6).monthPan[4] == 4);
    checkTrue("2020-01 monthStar=9", addYearMonth(2020, 1).monthPan[4] == 9);

    std::printf("== derived quantities (4.1 / redline 6) ==\n");
    for (int idx = 0; idx < 24; ++idx) {
        const FeixingResult r = computeFeixing(9, static_cast<float>(idx * 15));
        if (r.zuoIdx != idx || r.isJian) {
            ++failures;
            std::printf("  FAIL  deg=%d zuoIdx=%d isJian=%d\n", idx * 15, r.zuoIdx, static_cast<int>(r.isJian));
        }
    }
    std::printf("  PASS  all 24 center degrees map back to their mountain\n");
    checkTrue("offset 4.5 counts as forward", !computeFeixing(9, 4.5f).isJian);
    checkTrue("offset 4.6 counts as jian", computeFeixing(9, 4.6f).isJian);
    checkTrue("359.9 wraps to zi-shan forward", computeFeixing(9, 359.9f).zuoIdx == 0 && !computeFeixing(9, 359.9f).isJian);

    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
