#include "domain/LichunTable.h"

namespace fx::domain {

const std::array<int, 201> LICHUN_MONTHS = [] {
    std::array<int, 201> months{};
    months.fill(2);
    return months;
}();

int getLichunMonth(int year) {
    const int idx = year - 1900;
    if (idx < 0 || idx > 200) {
        return 2;
    }
    return LICHUN_MONTHS[static_cast<std::size_t>(idx)];
}

} // namespace fx::domain
