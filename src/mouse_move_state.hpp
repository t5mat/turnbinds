#pragma once

#include "common.hpp"

struct MouseMoveState
{
    long long update(bool reset, bool in_left, bool in_right, bool in_speed, double rate, double yawspeed, double anglespeedkey, double sensitivity, double yaw)
    {
        auto time = common::win32::performance_counter();

        if (reset) {
            prev_in_left = false;
            prev_in_right = false;
        }

        if ((prev_in_left ^ in_left) || (prev_in_right ^ in_right)) {
            last_time = time;
            remaining = 0.0;
        }

        prev_in_left = in_left;
        prev_in_right = in_right;

        if (!(in_left ^ in_right) || (time - last_time < common::win32::performance_counter_frequency() / rate)) {
            return 0;
        }

        remaining +=
            (
                (int(in_left) * -1 + int(in_right)) *
                (yawspeed / (sensitivity * yaw)) *
                (in_speed ? anglespeedkey : 1.0) *
                (time - last_time)
            ) / common::win32::performance_counter_frequency();

        auto amount = static_cast<long long>(remaining);
        remaining -= amount;
        last_time = time;
        return amount;
    }

private:
    long long last_time;
    double remaining;
    bool prev_in_left;
    bool prev_in_right;
};
