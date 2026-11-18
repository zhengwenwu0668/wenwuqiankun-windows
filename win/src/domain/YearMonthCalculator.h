#pragma once

#include <array>

namespace fx::domain {

struct YearMonthResult {
    std::array<int, 9> yearPan{};
    std::array<int, 9> monthPan{};
};

YearMonthResult addYearMonth(int year, int month);

} // namespace fx::domain
