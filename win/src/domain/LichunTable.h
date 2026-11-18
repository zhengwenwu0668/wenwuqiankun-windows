#pragma once

#include <array>

namespace fx::domain {

// 1900–2100 的立春月份（下标 0 → 1900）。该区间内立春均落在 2 月。
extern const std::array<int, 201> LICHUN_MONTHS;

int getLichunMonth(int year);

} // namespace fx::domain
