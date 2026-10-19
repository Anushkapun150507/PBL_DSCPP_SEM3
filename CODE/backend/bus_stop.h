// bus_stop.h -- one row of the bus schedule, modelled as a class.
#pragma once
#include <array>
#include <string>
#include "text_utils.h"

namespace unigo {

class BusStop {
public:
    static constexpr int SHIFTS = 3;

    BusStop(std::string place, std::string departFrom, std::array<std::string, SHIFTS> shifts)
        : place_(std::move(place)), departFrom_(std::move(departFrom)), shifts_(std::move(shifts)) {}

    const std::string& place() const { return place_; }
    const std::string& departFrom() const { return departFrom_; }
    const std::string& shift(int i) const { return shifts_.at(i); }      // .at() = bounds-checked
    bool runsIn(int i) const { return toMinutes(shifts_.at(i)) >= 0; }   // false for "No Service" / blank

    // Index of the earliest shift leaving at/after `nowMin`; -1 if the last bus has gone (or none run).
    int nextShift(int nowMin) const {
        int best = -1, bestTime = 24 * 60;
        for (int i = 0; i < SHIFTS; ++i) {
            int t = toMinutes(shifts_[i]);
            if (t >= nowMin && t < bestTime) { best = i; bestTime = t; }
        }
        return best;
    }

private:   // encapsulation: the data is read-only from outside, so it can't be corrupted by mistake
    std::string place_, departFrom_;
    std::array<std::string, SHIFTS> shifts_;   // fixed size: the schedule always has exactly 3 shifts
};

} // namespace unigo
