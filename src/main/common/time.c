/*
 * This file is part of INAV.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License Version 3, as described below:
 *
 * This file is free software: you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
 *
 * @author Alberto Garcia Hierro <alberto@garciahierro.com>
 */

#include "common/maths.h"
#include "common/printf.h"
#include "common/time.h"

#include "config/parameter_group_ids.h"

#include "drivers/time.h"

#define UNIX_REFERENCE_YEAR 1970
#define MILLIS_PER_SECOND 1000

// rtcTime_t when the system was started.
// Calculated in rtcSet().
static rtcTime_t started = 0;

static const uint16_t days[4][12] =
{
    {   0,  31,     60,     91,     121,    152,    182,    213,    244,    274,    305,    335},
    { 366,  397,    425,    456,    486,    517,    547,    578,    609,    639,    670,    700},
    { 731,  762,    790,    821,    851,    882,    912,    943,    974,    1004,   1035,   1065},
    {1096,  1127,   1155,   1186,   1216,   1247,   1277,   1308,   1339,   1369,   1400,   1430},
};

PG_REGISTER_WITH_RESET_TEMPLATE(timeConfig_t, timeConfig, PG_TIME_CONFIG, 0);

PG_RESET_TEMPLATE(timeConfig_t, timeConfig,
    .tz_offset = 0,
);

static rtcTime_t dateTimeToRtcTime(dateTime_t *dt)
{
    unsigned int second = dt->seconds;  // 0-59
    unsigned int minute = dt->minutes;  // 0-59
    unsigned int hour = dt->hours;      // 0-23
    unsigned int day = dt->day - 1;     // 0-30
    unsigned int month = dt->month - 1; // 0-11
    unsigned int year = dt->year - UNIX_REFERENCE_YEAR; // 0-99
    int32_t unixTime = (((year / 4 * (365 * 4 + 1) + days[year % 4][month] + day) * 24 + hour) * 60 + minute) * 60 + second;
    return rtcTimeMake(unixTime, dt->millis);
}

static void rtcTimeToDateTime(dateTime_t *dt, rtcTime_t t)
{
    int32_t unixTime = t / MILLIS_PER_SECOND;
    dt->seconds = unixTime % 60;
    unixTime /= 60;
    dt->minutes = unixTime % 60;
    unixTime /= 60;
    dt->hours = unixTime % 24;
    unixTime /= 24;

    unsigned int years = unixTime / (365 * 4 + 1) * 4;
    unixTime %= 365 * 4 + 1;

    unsigned int year;
    for (year = 3; year > 0; year--) {
        if (unixTime >= days[year][0]) {
            break;
        }
    }

    unsigned int month;
    for (month = 11; month > 0; month--) {
        if (unixTime >= days[year][month]) {
            break;
        }
    }

    dt->year = years + year + UNIX_REFERENCE_YEAR;
    dt->month = month + 1;
    dt->day = unixTime - days[year][month] + 1;
    dt->millis = t % MILLIS_PER_SECOND;
}

static void dateTimeFormat(char *buf, dateTime_t *dateTime, int16_t offset)
{
    dateTime_t local;
    rtcTime_t utcTime;
    rtcTime_t localTime;

    int tz_hours = 0;
    int tz_minutes = 0;

    if (offset != 0) {
        tz_hours = offset / 60;
        tz_minutes = ABS(offset % 60);
        utcTime = dateTimeToRtcTime(dateTime);
        localTime = rtcTimeMake(rtcTimeGetSeconds(&utcTime) + offset * 60, rtcTimeGetMillis(&utcTime));
        rtcTimeToDateTime(&local, localTime);
        dateTime = &local;
    }
    tfp_sprintf(buf, "%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02d:%02d",
        dateTime->year, dateTime->month, dateTime->day,
        dateTime->hours, dateTime->minutes, dateTime->seconds, dateTime->millis,
        tz_hours >= 0 ? '+' : '-', ABS(tz_hours), tz_minutes);
}

rtcTime_t rtcTimeMake(int32_t secs, uint16_t millis)
{
    return ((rtcTime_t)secs) * MILLIS_PER_SECOND + millis;
}

int32_t rtcTimeGetSeconds(rtcTime_t *t)
{
    return *t / MILLIS_PER_SECOND;
}

uint16_t rtcTimeGetMillis(rtcTime_t *t)
{
    return *t % MILLIS_PER_SECOND;
}

void dateTimeFormatUTC(char *buf, dateTime_t *dt)
{
    dateTimeFormat(buf, dt, 0);
}

void dateTimeFormatLocal(char *buf, dateTime_t *dt)
{
    dateTimeFormat(buf, dt, timeConfig()->tz_offset);
}

bool rtcHasTime()
{
    return started != 0;
}

bool rtcGet(rtcTime_t *t)
{
    if (!rtcHasTime()) {
        return false;
    }
    *t = started + millis();
    return true;
}

bool rtcSet(rtcTime_t *t)
{
    started = *t - millis();
    return true;
}

bool rtcGetDateTime(dateTime_t *dt)
{
    rtcTime_t t;
    if (rtcGet(&t)) {
        rtcTimeToDateTime(dt, t);
        return true;
    }
    // No time stored, fill dt with 0000-01-01T00:00:00.000
    dt->year = 0;
    dt->month = 1;
    dt->day = 1;
    dt->hours = 0;
    dt->minutes = 0;
    dt->seconds = 0;
    dt->millis = 0;
    return false;
}

bool rtcSetDateTime(dateTime_t *dt)
{
    rtcTime_t t = dateTimeToRtcTime(dt);
    return rtcSet(&t);
}