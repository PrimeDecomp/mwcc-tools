// The Mac emulation's date and time services (Wii's "DateTimeUtils" module, which holds TickCount,
// right after COSToolsCLT as in 2.6; Pro7's MacEmul has no such file).
#include "driver/DateTimeUtils.h"
#include "driver/MsDos.h"
#include <stdio.h>

// The date and time formatting below has no caller: the linker drops its code but keeps its data,
// which is the original's (these tables, then its sprintf formats).
static int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static char weekday_offsets[] = "-bed=pen+mad.";
static char* day_names[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
// clang-format off
static char* month_names[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September",
                                "October", "November", "December"};
// clang-format on

// The date SECONDS (since the start of 1904) is, as the Mac's IUDateString writes it in FORM:
// short, long or abbreviated.
// TODO: invented name and signature (the Mac's IUDateString, whose DateForm and output the body
// follows); the original's code is not in the binary, only its data
void IUDateString(UInt32 seconds, SInt16 form, char* text) {
    UInt32 days = seconds / 86400;
    int year = 1904;
    int month = 0;
    int length;
    int weekday;

    for (;;) {
        length = (year & 3) ? 365 : 366;
        if (days < length) {
            break;
        }
        days -= length;
        year++;
    }
    for (;;) {
        length = month_days[month] + (month == 1 && !(year & 3));
        if (days < length) {
            break;
        }
        days -= length;
        month++;
    }
    weekday =
        (days + 1 + weekday_offsets[month + 1] + year - (month < 2) + (year - (month < 2)) / 4) % 7;
    switch (form) {
        case 0:
            sprintf(text, "%d/%d/%d", month + 1, days + 1, year % 100);
            break;
        case 1:
            sprintf(text, "%s, %s %d, %d", day_names[weekday], month_names[month], days + 1, year);
            break;
        default:
            sprintf(
                text, "%.3s, %.3s %d, %d", day_names[weekday], month_names[month], days + 1, year
            );
            break;
    }
}

// The time of day SECONDS is, as the Mac's IUTimeString writes it.
// TODO: invented name and signature (the Mac's IUTimeString, whose wantSeconds the body follows);
// the original's code is not in the binary, only its data
void IUTimeString(UInt32 seconds, Boolean wantSeconds, char* text) {
    int hour = (seconds / 3600) % 24;
    int minute = (seconds / 60) % 60;
    char half = (hour < 12) ? 'A' : 'P';

    hour %= 12;
    if (hour == 0) {
        hour = 12;
    }
    if (wantSeconds) {
        sprintf(text, "%2d:%02d:%02d %cM", hour, minute, (int)(seconds % 60), half);
    } else {
        sprintf(text, "%2d:%02d %cM", hour, minute, half);
    }
}

// The Mac's ticks (sixtieths of a second); __stdcall as Wii's `_TickCount@0`, like the other Mac
// emulation calls
UInt32 __stdcall TickCount(void) {
    return OS_GetMilliseconds() * 60 / 1000;
}
