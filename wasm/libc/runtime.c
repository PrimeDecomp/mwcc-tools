/* The runtime around the compiler: startup (the standard handles, the environment, the thread data, the command
 * line's arguments), exit with its atexit functions, signal, getenv, and the time functions, as MSL Win32's, with
 * the same allocations in the same order. */
#include "libc.h"

/* --- startup */

char** _Environ;

void _CRTStartup(void)
{
    static int initialized;
    static char* environment;
    char* strings;
    char* start;
    char* end;
    char* p;
    int count = 0;
    int i;

    if (initialized++) {
        return;
    }
    for (i = 0; i < 3; i++) {
        _HandleTable[i] = malloc(sizeof(FileStruct));
        if (_HandleTable[i]) {
            _HandleTable[i]->handle = GetStdHandle(i == 0 ? STD_INPUT_HANDLE : i == 1 ? STD_OUTPUT_HANDLE :
                                                                                         STD_ERROR_HANDLE);
            _HandleTable[i]->translate = 0;
            _HandleTable[i]->append = 0;
        }
    }
    _HandPtr = 3;

    /* the environment's strings, but those starting with "=" (a drive's directory) */
    strings = GetEnvironmentStrings();
    start = GetEnvironmentStrings();
    while (*start == '=') {
        start += strlen(start) + 1;
    }
    end = start;
    while (*end) {
        end += strlen(end) + 1;
    }
    environment = malloc(end - start);
    if (environment) {
        memcpy(environment, start, end - start);
    }
    for (p = environment; *p; p += strlen(p) + 1) {
        count++;
    }
    _Environ = malloc((count + 1) * sizeof(char*));
    i = 0;
    for (p = environment; *p; p += strlen(p) + 1) {
        _Environ[i++] = p;
    }
    _Environ[i] = 0;
    FreeEnvironmentStringsA(strings);
}

char* getenv(const char* name)
{
    int length = strlen(name);
    char* entry;
    int i = 0;

    while ((entry = _Environ[i++]) != 0) {
        if (entry[length] == '=' && _strnicmp(entry, name, length) == 0) {
            return entry + length + 1;
        }
    }
    return 0;
}

/* --- thread data */

static ThreadData* thread_data;

static int initialize_thread_data(void)
{
    ThreadData* data = malloc(sizeof(ThreadData));

    if (!data) {
        return 0;
    }
    data->error = 0;
    data->random_next = 1;
    data->strtok_n = (unsigned char*)"";
    data->strtok_s = (unsigned char*)"";
    data->thread_handle = 0;
    thread_data = data;
    return 1;
}

static void dispose_thread_data_index(void)
{
}

int _InitializeMainThreadData(void)
{
    static int index;

    if (!index) {
        index = 1;
        if (atexit(dispose_thread_data_index)) {
            return 0;
        }
    }
    return initialize_thread_data();
}

ThreadData* __thread_data(void)
{
    if (thread_data == 0) {
        initialize_thread_data();
    }
    if (thread_data == 0) {
        static const char message[] = "Could not get thread local data\n";
        DWORD written;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1, &written, 0);
        exit(7);
    }
    return thread_data;
}

/* (the compiler's units declare it with their own type: gc2's include/libc/errno.h) */
void* _GetThreadLocalData(void)
{
    return __thread_data();
}

/* --- the command line */

int __argc;
char** __argv;
static char* command_line;

static void free_command_line(void)
{
    if (command_line != 0) {
        free(command_line);
    }
    if (__argv != 0 && __argc != 0) {
        free(__argv);
    }
}

static void memory_failure(void)
{
    static char* no_arguments[] = {0};

    __argv = no_arguments;
    __argc = 0;
}

/* Splits the command line as MSL does: blanks separate arguments, quotes group them ("" inside quotes is a quote),
 * and backslashes are literal but before a quote (2n of them and a quote are n and the quote's meaning; 2n+1 are n
 * and a literal quote). */
void _SetupArgs(void)
{
    static const char blanks[] = " \t\r\n\v";
    char* in;
    char* out;
    int max_argc = 1;
    int quoting;

    __argc = 0;
    __argv = malloc(sizeof(char*));
    if (__argv == 0) {
        memory_failure();
        return;
    }
    in = GetCommandLineA();
    out = command_line = malloc(strlen(in) + 1);
    if (out == 0) {
        memory_failure();
        return;
    }
    while (strchr(blanks, *in) != 0) {
        in++;
    }
    while (*in != 0) {
        if (__argc + 1 >= max_argc) {
            char** arguments;
            max_argc += 16;
            arguments = realloc(__argv, max_argc * sizeof(char*));
            if (arguments == 0) {
                break;
            }
            __argv = arguments;
        }
        __argv[__argc++] = out;
        quoting = 0;
        while (*in != 0) {
            if (!quoting) {
                if (strchr(blanks, *in)) {
                    do {
                        in++;
                    } while (*in != 0 && strchr(blanks, *in));
                    *out++ = 0;
                    break;
                }
                if (*in == '"') {
                    in++;
                    quoting = 1;
                    continue;
                }
            } else {
                if (*in == '"' && in[1] == '"') {
                    *out++ = '"';
                    in += 2;
                    continue;
                }
                if (*in == '"') {
                    in++;
                    quoting = 0;
                    continue;
                }
            }
            if (*in == '\\' && in[1] == '\\') {
                int n = 2;
                while (in[n] == '\\') {
                    n++;
                }
                if (in[n] == '"') {
                    if ((n &= ~1) != 0) {
                        do {
                            *out++ = *in;
                            in += 2;
                            n -= 2;
                        } while (n != 0);
                    }
                } else {
                    while (n--) {
                        *out++ = *in++;
                    }
                }
                continue;
            }
            if (*in == '\\' && in[1] == '"') {
                *out++ = '"';
                in += 2;
                continue;
            }
            *out++ = *in++;
        }
    }
    *out = 0;
    __argv[__argc] = 0;
    atexit(free_command_line);
}

/* --- exit */

#define MAX_ATEXIT 64

static void (*atexit_funcs[MAX_ATEXIT])(void);
static int atexit_count;
static int aborting;

int atexit(void (*func)(void))
{
    if (atexit_count == MAX_ATEXIT) {
        return -1;
    }
    atexit_funcs[atexit_count++] = func;
    return 0;
}

void exit(int status)
{
    if (!aborting) {
        while (atexit_count > 0) {
            atexit_funcs[--atexit_count]();
        }
        if (__stdio_exit) {
            __stdio_exit();
            __stdio_exit = 0;
        }
    }
    ExitProcess(status);
}

/* signal 1 to 6; a handler of 1 ignores the signal, 0 is the default */
static __signal_func_ptr signal_funcs[6];

__signal_func_ptr signal(int sig, __signal_func_ptr func)
{
    __signal_func_ptr old;

    if (sig < 1 || sig > 6) {
        LIBC_ERRNO = LIBC_ESIGPARM;
        return (__signal_func_ptr)-1;
    }
    old = signal_funcs[sig - 1];
    signal_funcs[sig - 1] = func;
    return old;
}

int raise(int sig)
{
    __signal_func_ptr func;

    if (sig < 1 || sig > 6) {
        return -1;
    }
    func = signal_funcs[sig - 1];
    if (func != (__signal_func_ptr)1) {
        signal_funcs[sig - 1] = 0;
    }
    if (func == (__signal_func_ptr)1 || (func == 0 && sig == SIGABRT)) {
        return 0;
    }
    if (func == 0) {
        exit(0);
    }
    func(sig);
    return 0;
}

void abort(void)
{
    raise(SIGABRT);
    aborting = 1;
    exit(1);
}

/* --- time: seconds since 1970 from the local time as UTC (MSL's), its tm from 1900 */

static const short month_to_days[2][13] = {
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
    {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366},
};
static const char* const day_name[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* const month_name[] = {"January", "February", "March", "April", "May", "June", "July", "August",
    "September", "October", "November", "December"};

/* seconds since 1970 of the local time, and milliseconds */
static long local_seconds(int* milliseconds)
{
    static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    SYSTEMTIME t;
    long years, leap_years, days, i;

    GetLocalTime(&t);
    years = t.wYear - 1970;
    leap_years = years;
    if (t.wMonth > 2) {
        leap_years++;
    }
    days = years * 365 + (leap_years + 1) / 4;
    for (i = 1; i < t.wMonth; i++) {
        days += month_days[i - 1];
    }
    days += t.wDay - 1;
    *milliseconds = t.wMilliseconds;
    return ((days * 24 + t.wHour) * 60 + t.wMinute) * 60 + t.wSecond;
}

clock_t clock(void)
{
    int milliseconds;
    long seconds = local_seconds(&milliseconds);

    return seconds * 1000 + milliseconds;
}

time_t time(time_t* timer)
{
    int milliseconds;
    time_t t = local_seconds(&milliseconds);
    TIME_ZONE_INFORMATION tzi;

    if (GetTimeZoneInformation(&tzi) != 0) {
        t += tzi.Bias * 60;
    }
    if (timer) {
        *timer = t;
    }
    return t;
}

/* MSL's modulo and division, rounding toward minus infinity */
static int floor_div(int x, int y, int* rem)
{
    int q = x / y;
    int r = x % y;

    if (r && ((r < 0) != (y < 0))) {
        q -= 1;
        r += y;
    }
    *rem = r;
    return q;
}

static int leap_year(int year)
{
    int r4, r100, r400;

    floor_div(year, 4, &r4);
    floor_div(year, 100, &r100);
    floor_div(year, 400, &r400);
    return r4 == 0 && (r100 != 0 || r400 == 100);
}

static void time2tm(time_t in, struct tm* tm)
{
    unsigned long t = in + ((365 * 70UL) + 17) * 24 * 60 * 60;
    unsigned long days, seconds, years, months;
    int leap;

    tm->tm_isdst = -1;
    days = t / 86400;
    seconds = t % 86400;
    tm->tm_wday = (days + 1) % 7;
    years = 0;
    for (;;) {
        unsigned long days_this_year = leap_year(years) ? 366 : 365;
        if (days < days_this_year) {
            break;
        }
        days -= days_this_year;
        years += 1;
    }
    tm->tm_year = years;
    tm->tm_yday = days;
    months = 0;
    leap = leap_year(years);
    for (;;) {
        if (days < (unsigned long)month_to_days[leap][months + 1]) {
            days -= month_to_days[leap][months];
            break;
        }
        ++months;
    }
    tm->tm_mon = months;
    tm->tm_mday = days + 1;
    tm->tm_hour = seconds / 3600;
    seconds %= 3600;
    tm->tm_min = seconds / 60;
    tm->tm_sec = seconds % 60;
}

struct tm* localtime(const time_t* timer)
{
    static struct tm tm;
    TIME_ZONE_INFORMATION tzi;
    time_t t;

    if (!timer) {
        tm.tm_sec = tm.tm_min = tm.tm_hour = 0;
        tm.tm_mday = 1;
        tm.tm_mon = tm.tm_year = 0;
        tm.tm_wday = 1;
        tm.tm_yday = 0;
        tm.tm_isdst = -1;
        return &tm;
    }
    t = *timer;
    if (GetTimeZoneInformation(&tzi) != 0) {
        t -= tzi.Bias * 60;
    }
    time2tm(t, &tm);
    return &tm;
}

/* adds Y to *X unless that overflows */
static int add(long* x, long y)
{
    long v = *x;

    if (y < 0 ? v < 0 && y < (-0x7FFFFFFF - 1) - v : v > 0 && y > 0x7FFFFFFF - v) {
        return 0;
    }
    *x = v + y;
    return 1;
}

/* moves whole multiples of Y from *X into *Z */
static int adjust(int* x, int y, int* z)
{
    int r;
    long sum = *z;
    int q = floor_div(*x, y, &r);

    *x = r;
    if (!add(&sum, q)) {
        return 0;
    }
    *z = sum;
    return 1;
}

static int leap_days(int year, int mon)
{
    int r;
    int n = floor_div(year, 4, &r) - floor_div(year, 100, &r);

    if (year < 100) {
        n += floor_div(year + 899, 1000, &r);
    } else {
        n += floor_div(year - 100, 1000, &r) + 1;
    }
    if (leap_year(year)) {
        if (year < 0) {
            if (mon > 1) {
                ++n;
            }
        } else if (mon <= 1) {
            --n;
        }
    }
    return n;
}

static int tm2time(struct tm* tm, time_t* t)
{
    long days;
    unsigned long seconds, day_secs;

    --tm->tm_mday;
    if (!adjust(&tm->tm_sec, 60, &tm->tm_min) || !adjust(&tm->tm_min, 60, &tm->tm_hour) ||
        !adjust(&tm->tm_hour, 24, &tm->tm_mday) || !adjust(&tm->tm_mon, 12, &tm->tm_year)) {
        return 0;
    }
    days = tm->tm_year;
    if (days > 0x7FFFFFFF / 365 || days < -(0x7FFFFFFF / 365)) {
        return 0;
    }
    days *= 365;
    if (!add(&days, leap_days(tm->tm_year, tm->tm_mon)) || !add(&days, month_to_days[0][tm->tm_mon]) ||
        !add(&days, tm->tm_mday)) {
        return 0;
    }
    if (days < 0 || (unsigned long)days > 0xFFFFFFFFu / 86400) {
        return 0;
    }
    day_secs = days * 86400;
    seconds = tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
    if (seconds > 0xFFFFFFFFu - day_secs) {
        return 0;
    }
    seconds += day_secs;
    seconds -= ((365 * 70UL) + 17) * 24 * 60 * 60;
    *t = seconds;
    time2tm(seconds, tm);
    return 1;
}

time_t mktime(struct tm* timeptr)
{
    struct tm tm = *timeptr;
    time_t t;

    if (!tm2time(&tm, &t)) {
        return (time_t)-1;
    }
    *timeptr = tm;
    return t;
}

/* writes FORMAT's conversion when SIZE characters fit in what is left */
static int emit(char* str, size_t size, size_t* max_size, const char* format, ...)
{
    va_list arg;

    if (size > *max_size) {
        return 0;
    }
    *max_size -= size;
    va_start(arg, format);
    return vsprintf(str, format, arg);
}

static int week_num(const struct tm* tm, int starting_day)
{
    int r;
    int days = tm->tm_yday;

    floor_div(tm->tm_wday - starting_day, 7, &r);
    days -= r;
    if (days < 0) {
        return 0;
    }
    return days / 7 + 1;
}

/* strftime's conversions but the ISO 8601 weeks (%g, %G, %V) and the time zone (%z), which give nothing */
size_t strftime(char* str, size_t max_size, const char* format_str, const struct tm* timeptr)
{
    static const struct tm default_tm = {0, 0, 0, 1, 0, 0, 1, 0, -1};
    struct tm tm;
    size_t num_chars, chars_written, space_remaining;
    const char* format_ptr;
    const char* curr_format;
    int n;

    if ((space_remaining = --max_size) <= 0) {
        return 0;
    }
    tm = default_tm;
    if (timeptr) {
        tm = *timeptr;
        if (mktime(&tm) == (time_t)-1) {
            tm = default_tm;
        }
    }
    format_ptr = format_str;
    chars_written = 0;
    while (*format_ptr) {
        if (!(curr_format = strchr(format_ptr, '%'))) {
            if ((num_chars = strlen(format_ptr)) != 0) {
                if (num_chars > space_remaining) {
                    return 0;
                }
                memcpy(str, format_ptr, num_chars);
                chars_written += num_chars;
                str += num_chars;
                space_remaining -= num_chars;
            }
            break;
        }
        if ((num_chars = curr_format - format_ptr) != 0) {
            if (num_chars > space_remaining) {
                return 0;
            }
            memcpy(str, format_ptr, num_chars);
            chars_written += num_chars;
            str += num_chars;
            space_remaining -= num_chars;
        }
        format_ptr = curr_format;
        switch (*++format_ptr) {
        case 'a':
            num_chars = emit(str, 3, &space_remaining, "%.3s", day_name[tm.tm_wday]);
            break;
        case 'A':
            num_chars = emit(str, strlen(day_name[tm.tm_wday]), &space_remaining, "%s", day_name[tm.tm_wday]);
            break;
        case 'b':
            num_chars = emit(str, 3, &space_remaining, "%.3s", month_name[tm.tm_mon]);
            break;
        case 'B':
            num_chars = emit(str, strlen(month_name[tm.tm_mon]), &space_remaining, "%s", month_name[tm.tm_mon]);
            break;
        case 'c':
            num_chars = strftime(str, space_remaining + 1, "%A %B %d %T %Y", &tm);
            space_remaining -= num_chars;
            break;
        case 'd':
            num_chars = emit(str, 2, &space_remaining, "%.2d", tm.tm_mday);
            break;
        case 'F':
            num_chars = emit(str, 10, &space_remaining, "%.4d-%.2d-%.2d", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday);
            break;
        case 'H':
            num_chars = emit(str, 2, &space_remaining, "%.2d", tm.tm_hour);
            break;
        case 'I':
            num_chars = emit(str, 2, &space_remaining, "%.2d", (n = tm.tm_hour % 12) ? n : 12);
            break;
        case 'j':
            num_chars = emit(str, 3, &space_remaining, "%.3d", tm.tm_yday + 1);
            break;
        case 'm':
            num_chars = emit(str, 2, &space_remaining, "%.2d", tm.tm_mon + 1);
            break;
        case 'M':
            num_chars = emit(str, 2, &space_remaining, "%.2d", tm.tm_min);
            break;
        case 'p':
            num_chars = emit(str, 2, &space_remaining, "%.2s", tm.tm_hour < 12 ? "am" : "pm");
            break;
        case 'r':
            num_chars = strftime(str, space_remaining + 1, "%I:%M:%S %p", &tm);
            space_remaining -= num_chars;
            break;
        case 'R':
            num_chars = strftime(str, space_remaining + 1, "%H:%M", &tm);
            space_remaining -= num_chars;
            break;
        case 'S':
            num_chars = emit(str, 2, &space_remaining, "%.2d", tm.tm_sec);
            break;
        case 'u':
            if (tm.tm_wday == 0) {
                num_chars = emit(str, 1, &space_remaining, "7");
            } else {
                num_chars = emit(str, 1, &space_remaining, "%.1d", tm.tm_wday);
            }
            break;
        case 'U':
            num_chars = emit(str, 2, &space_remaining, "%.2d", week_num(&tm, 0));
            break;
        case 'w':
            num_chars = emit(str, 1, &space_remaining, "%.1d", tm.tm_wday);
            break;
        case 'W':
            num_chars = emit(str, 2, &space_remaining, "%.2d", week_num(&tm, 1));
            break;
        case 'x':
            num_chars = strftime(str, space_remaining + 1, "%A %B %d %Y", &tm);
            space_remaining -= num_chars;
            break;
        case 'T':
        case 'X':
            num_chars = strftime(str, space_remaining + 1, "%H:%M:%S", &tm);
            space_remaining -= num_chars;
            break;
        case 'y':
            num_chars = emit(str, 2, &space_remaining, "%.2d", tm.tm_year % 100);
            break;
        case 'Y':
            num_chars = emit(str, 4, &space_remaining, "%.4d", tm.tm_year + 1900);
            break;
        case 'Z':
            ++format_ptr;
            continue;
        case '%':
            num_chars = emit(str, 2, &space_remaining, "%%");
            break;
        default:
            num_chars = emit(str, 2, &space_remaining, "%%%c", *format_ptr);
            break;
        }
        if (!num_chars) {
            return 0;
        }
        chars_written += num_chars;
        str += num_chars;
        ++format_ptr;
    }
    *str = 0;
    return max_size < chars_written ? 0 : chars_written;
}

/* --- assert (MSL's console version, which 2.x links) */

void __assertion_failed(char const* condition, char const* filename, int lineno)
{
    fprintf(stderr, "Assertion (%s) failed in \"%s\" on line %d\n", condition, filename, lineno);
    abort();
}
