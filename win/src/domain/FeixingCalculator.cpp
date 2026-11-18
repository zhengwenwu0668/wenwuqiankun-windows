#include "domain/FeixingCalculator.h"

#include <cmath>
#include <cstddef>
#include <string>

#include "domain/Constants.h"

namespace fx::domain {

std::array<int, 9> flyForward(int center) {
    std::array<int, 9> pan{};
    int num = center;
    for (int i = 0; i < 9; ++i) {
        pan[static_cast<std::size_t>(FLY_ORDER[static_cast<std::size_t>(i)])] = num;
        num = (num == 9) ? 1 : num + 1;
    }
    return pan;
}

namespace {

std::array<int, 9> flyBackward(int center) {
    std::array<int, 9> pan{};
    int num = center;
    for (int i = 0; i < 9; ++i) {
        pan[static_cast<std::size_t>(FLY_ORDER[static_cast<std::size_t>(i)])] = num;
        num = (num == 1) ? 9 : num - 1;
    }
    return pan;
}

// 顺逆按「原入中数所在宫的同元龙字符」判定；兼向只换入中数（替卦表同样以该同元龙字符为键），
// 原入中为 5 时不替，且改按坐向字符本身的阴阳定顺逆。
std::array<int, 9> flyPan(int originCenter, const std::string &mountainChar, bool isJian) {
    const std::string refChar =
        (originCenter == 5) ? mountainChar : dragonChar(originCenter, dragonOf(mountainChar));
    const bool isShun = !isYin(refChar);
    const int center = (isJian && originCenter != 5) ? tiGua(refChar, originCenter) : originCenter;
    return isShun ? flyForward(center) : flyBackward(center);
}

} // namespace

FeixingResult computeFeixing(int yun, float deg) {
    FeixingResult result;
    const float normalizedDeg = std::fmod(std::fmod(deg, 360.0f) + 360.0f, 360.0f);
    const int zuoIdx = static_cast<int>(std::floor(std::fmod(static_cast<double>(normalizedDeg) + 7.5, 360.0) / 15.0)) % 24;
    const int centerDeg = zuoIdx * 15;
    const float offset =
        std::fmod(std::fmod(normalizedDeg - static_cast<float>(centerDeg) + 180.0f, 360.0f) + 360.0f, 360.0f) - 180.0f;
    const bool isJian = std::abs(offset) > 4.5f;
    const int xiangIdx = (zuoIdx + 12) % 24;

    const std::array<int, 9> yunPan = flyForward(yun);
    const int zuoPalace = MOUNTAIN_TO_PALACE[static_cast<std::size_t>(zuoIdx)];
    const int xiangPalace = MOUNTAIN_TO_PALACE[static_cast<std::size_t>(xiangIdx)];

    const int shanOriginCenter = yunPan[static_cast<std::size_t>(zuoPalace)];
    const int xiangOriginCenter = yunPan[static_cast<std::size_t>(xiangPalace)];

    result.zuoIdx = zuoIdx;
    result.zuoDeg = normalizedDeg;
    result.isJian = isJian;
    result.yunPan = yunPan;
    result.shanPan = flyPan(shanOriginCenter, MOUNTAINS[static_cast<std::size_t>(zuoIdx)], isJian);
    result.xiangPan = flyPan(xiangOriginCenter, MOUNTAINS[static_cast<std::size_t>(xiangIdx)], isJian);
    return result;
}

} // namespace fx::domain
