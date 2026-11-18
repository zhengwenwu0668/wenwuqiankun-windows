#pragma once

#include <array>

namespace fx::domain {

struct FeixingResult {
    int zuoIdx = 0;
    float zuoDeg = 0.0f;
    bool isJian = false;
    std::array<int, 9> yunPan{};
    std::array<int, 9> shanPan{};
    std::array<int, 9> xiangPan{};
};

// 洛书顺飞：按 FLY_ORDER 宫序从入中数起，9 后回到 1。运盘、山/向盘与流年流月盘共用。
std::array<int, 9> flyForward(int center);

FeixingResult computeFeixing(int yun, float deg);

} // namespace fx::domain
