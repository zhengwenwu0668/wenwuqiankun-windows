#include "domain/YearMonthCalculator.h"

#include "domain/FeixingCalculator.h"
#include "domain/LichunTable.h"

namespace fx::domain {

YearMonthResult addYearMonth(int year, int month) {
    const int lichunMonth = getLichunMonth(year);
    const int starYear = (month < lichunMonth) ? year - 1 : year;

    int yearStar = (11 - (starYear % 9)) % 9;
    if (yearStar == 0) {
        yearStar = 9;
    }

    // 1 月是上一年十二月，年支取 year−1。
    const int zhiYear = (month == 1) ? year - 1 : year;
    const int zhi = ((zhiYear - 4) % 12 + 12) % 12;
    const int firstMonthStar = (zhi % 3 == 0) ? 8 : ((zhi % 3 == 1) ? 5 : 2);

    const int monthIdx = (month == 1) ? 12 : month - 1;
    const int monthStar = ((firstMonthStar - (monthIdx - 1) - 1) % 9 + 9) % 9 + 1;

    YearMonthResult result;
    result.yearPan = flyForward(yearStar);
    result.monthPan = flyForward(monthStar);
    return result;
}

} // namespace fx::domain
