/* The number scanners (strtol, atoi, strtod) and sscanf, as MSL's: state machines over a reading procedure. strtod
 * keeps the first 20 significant digits, makes the value with __dec2num (x87.c, the original's conversion), and
 * gives 0 for what is below the smallest normal double. sscanf has the integer conversions the compiler uses (%d,
 * %i, %u, %o, %x and %n, with widths, * and h, hh, l). */
#include "libc.h"

#define is_space(c) (__ctype_map[(unsigned char)(c)] & 0x06)
#define is_digit(c) (__ctype_map[(unsigned char)(c)] & 0x10)
#define is_alpha(c) (__ctype_map[(unsigned char)(c)] & 0xC0)

/* MSL's string reader: it reads characters as (signed) char, so a character above 0x7F reads as a negative number */
int __StringRead(void* input, int c, int action)
{
    StringInput* in = input;
    char result;

    switch (action) {
    case READ_GET:
        result = *in->next;
        if (result == 0) {
            in->null_seen = 1;
            return -1;
        }
        in->next++;
        return result;
    case READ_UNGET:
        if (!in->null_seen) {
            in->next--;
        } else {
            in->null_seen = 0;
        }
        return c;
    case READ_TEST:
        return in->null_seen;
    }
    return 0;
}

enum integer_states {
    int_start = 0x01,
    check_for_zero = 0x02,
    leading_zero = 0x04,
    need_digit = 0x08,
    digit_loop = 0x10,
    int_finished = 0x20,
    int_failure = 0x40
};

#define get() (count++, read(argument, 0, READ_GET))

unsigned long __strtoul(int base, int max_width, ReadProc read, void* argument, int* chars_scanned, int* negative,
    int* overflow)
{
    int state = int_start;
    int count = 0;
    unsigned long value = 0;
    unsigned long value_max = 0;
    int c;

    *negative = *overflow = 0;
    if (base < 0 || base == 1 || base > 36 || max_width < 1) {
        state = int_failure;
    } else {
        c = get();
    }
    if (base) {
        value_max = 0xFFFFFFFFu / base;
    }
    while (count <= max_width && c != -1 && !(state & (int_finished | int_failure))) {
        switch (state) {
        case int_start:
            if (is_space(c)) {
                c = get();
                break;
            }
            if (c == '+') {
                c = get();
            } else if (c == '-') {
                c = get();
                *negative = 1;
            }
            state = check_for_zero;
            break;
        case check_for_zero:
            if ((base == 0 || base == 16) && c == '0') {
                state = leading_zero;
                c = get();
                break;
            }
            state = need_digit;
            break;
        case leading_zero:
            if (c == 'X' || c == 'x') {
                base = 16;
                state = need_digit;
                c = get();
                break;
            }
            if (base == 0) {
                base = 8;
            }
            state = digit_loop;
            break;
        case need_digit:
        case digit_loop:
            if (base == 0) {
                base = 10;
            }
            if (!value_max) {
                value_max = 0xFFFFFFFFu / base;
            }
            if (is_digit(c)) {
                if ((c -= '0') >= base) {
                    state = state == digit_loop ? int_finished : int_failure;
                    c += '0';
                    break;
                }
            } else if (!is_alpha(c) || (toupper(c) - 'A' + 10) >= base) {
                state = state == digit_loop ? int_finished : int_failure;
                break;
            } else {
                c = toupper(c) - 'A' + 10;
            }
            if (value > value_max) {
                *overflow = 1;
            }
            value *= base;
            if ((unsigned long)c > 0xFFFFFFFFu - value) {
                *overflow = 1;
            }
            value += c;
            state = digit_loop;
            c = get();
            break;
        }
    }
    if (!(state & (leading_zero | digit_loop | int_finished))) {
        count = value = 0;
    } else {
        count--;
    }
    *chars_scanned = count;
    read(argument, c, READ_UNGET);
    return value;
}

long strtol(const char* str, char** end, int base)
{
    unsigned long uvalue;
    long svalue;
    int count, negative, overflow;
    StringInput in;

    in.next = (char*)str;
    in.null_seen = 0;
    uvalue = __strtoul(base, 0x7FFFFFFF, __StringRead, &in, &count, &negative, &overflow);
    if (end) {
        *end = (char*)str + count;
    }
    if (overflow || (!negative && uvalue > 0x7FFFFFFFu) || (negative && uvalue > 0x80000000u)) {
        svalue = negative ? (long)0x80000000u : 0x7FFFFFFF;
        LIBC_ERRNO = LIBC_ERANGE;
    } else {
        svalue = negative ? -(long)uvalue : (long)uvalue;
    }
    return svalue;
}

int atoi(const char* str)
{
    return strtol(str, 0, 10);
}

enum float_states {
    float_start = 0x0001,
    sig_start = 0x0002,
    leading_sig_zeroes = 0x0004,
    int_digit_loop = 0x0008,
    frac_start = 0x0010,
    frac_digit_loop = 0x0020,
    sig_end = 0x0040,
    exp_start = 0x0080,
    leading_exp_digit = 0x0100,
    leading_exp_zeroes = 0x0200,
    exp_digit_loop = 0x0400,
    float_finished = 0x0800,
    float_failure = 0x1000
};

/* the largest and smallest normal doubles (long double is double) */
static const unsigned long double_max[2] = {0xFFFFFFFF, 0x7FEFFFFF};
static const unsigned long double_min[2] = {0, 0x00100000};
static const unsigned long double_huge[2] = {0, 0x7FF00000};
#define DOUBLE_MAX (*(const double*)double_max)
#define DOUBLE_MIN (*(const double*)double_min)
#define DOUBLE_HUGE (*(const double*)double_huge)

double __strtold(int max_width, ReadProc read, void* argument, int* chars_scanned, int* overflow)
{
    int state = float_start;
    int count = 0;
    int spaces = 0;
    int c;
    decimal d = {0, 0, 0, {0, ""}};
    int sig_negative = 0;
    int exp_negative = 0;
    long exp_value = 0;
    int exp_adjust = 0;
    double result;

    *overflow = 0;
    c = get();
    while (count <= max_width && c != -1 && !(state & (float_finished | float_failure))) {
        switch (state) {
        case float_start:
            if (is_space(c)) {
                c = get();
                count--;
                spaces++;
                break;
            }
            if (c == '+') {
                c = get();
            } else if (c == '-') {
                c = get();
                sig_negative = 1;
            }
            state = sig_start;
            break;
        case sig_start:
            if (c == '.') {
                state = frac_start;
                c = get();
                break;
            }
            if (!is_digit(c)) {
                state = float_failure;
                break;
            }
            if (c == '0') {
                state = leading_sig_zeroes;
                c = get();
                break;
            }
            state = int_digit_loop;
            break;
        case leading_sig_zeroes:
            if (c == '0') {
                c = get();
                break;
            }
            state = int_digit_loop;
            break;
        case int_digit_loop:
            if (!is_digit(c)) {
                if (c == '.') {
                    state = frac_digit_loop;
                    c = get();
                } else {
                    state = sig_end;
                }
                break;
            }
            if (d.sig.length < 20) {
                d.sig.text[d.sig.length++] = c;
            } else {
                exp_adjust++;
            }
            c = get();
            break;
        case frac_start:
            if (!is_digit(c)) {
                state = float_failure;
                break;
            }
            state = frac_digit_loop;
            break;
        case frac_digit_loop:
            if (!is_digit(c)) {
                state = sig_end;
                break;
            }
            if (d.sig.length < 20) {
                if (c != '0' || d.sig.length) {
                    d.sig.text[d.sig.length++] = c;
                }
                exp_adjust--;
            }
            c = get();
            break;
        case sig_end:
            if (c == 'E' || c == 'e') {
                state = exp_start;
                c = get();
                break;
            }
            state = float_finished;
            break;
        case exp_start:
            if (c == '+') {
                c = get();
            } else if (c == '-') {
                c = get();
                exp_negative = 1;
            }
            state = leading_exp_digit;
            break;
        case leading_exp_digit:
            if (!is_digit(c)) {
                state = float_failure;
                break;
            }
            if (c == '0') {
                state = leading_exp_zeroes;
                c = get();
                break;
            }
            state = exp_digit_loop;
            break;
        case leading_exp_zeroes:
            if (c == '0') {
                c = get();
                break;
            }
            state = exp_digit_loop;
            break;
        case exp_digit_loop:
            if (!is_digit(c)) {
                state = float_finished;
                break;
            }
            exp_value = exp_value * 10 + (c - '0');
            if (exp_value > 32767) {
                *overflow = 1;
            }
            c = get();
            break;
        }
    }

    if (!(state & (leading_sig_zeroes | int_digit_loop | frac_digit_loop | leading_exp_zeroes | exp_digit_loop |
            float_finished))) {
        count = 0;
        *chars_scanned = 0;
    } else {
        count--;
        *chars_scanned = count + spaces;
    }
    read(argument, c, READ_UNGET);

    if (exp_negative) {
        exp_value = -exp_value;
    }
    {
        int n = d.sig.length;
        unsigned char* p = &d.sig.text[n];
        while (n-- && *--p == '0') {
            exp_adjust++;
        }
        d.sig.length = n + 1;
        if (d.sig.length == 0) {
            d.sig.text[d.sig.length++] = '0';
        }
    }
    exp_value += exp_adjust;
    if (exp_value < -32768 || exp_value > 32767) {
        *overflow = 1;
    }
    if (*overflow) {
        if (exp_negative) {
            return 0.0;
        }
        return sig_negative ? -DOUBLE_HUGE : DOUBLE_HUGE;
    }
    d.exp = exp_value;
    result = __dec2num(&d);
    if (result != 0.0 && result < DOUBLE_MIN) {
        *overflow = 1;
        result = 0.0;
    } else if (result > DOUBLE_MAX) {
        *overflow = 1;
        result = DOUBLE_HUGE;
    }
    if (sig_negative) {
        result = -result;
    }
    return result;
}

double strtod(const char* str, char** end)
{
    double value, abs_value;
    int count, overflow;
    StringInput in;

    in.next = (char*)str;
    in.null_seen = 0;
    value = __strtold(0x7FFFFFFF, __StringRead, &in, &count, &overflow);
    if (end) {
        *end = (char*)str + count;
    }
    abs_value = value < 0 ? -value : value;
    if (overflow || (value != 0.0 && (abs_value < DOUBLE_MIN || abs_value > DOUBLE_MAX))) {
        LIBC_ERRNO = LIBC_ERANGE;
    }
    return value;
}

#undef get

enum scan_arguments { scan_normal, scan_char, scan_short, scan_long };

int sscanf(const char* s, const char* format, ...)
{
    StringInput in;
    va_list arg;
    const char* f = format;
    int chars_read = 0;
    int items_assigned = 0;
    int conversions = 0;
    int c;

    in.next = (char*)s;
    in.null_seen = 0;
    va_start(arg, format);
    while (*f) {
        int suppress = 0;
        int width = 0x7FFFFFFF;
        int argument = scan_normal;
        int base, negative, overflow, num_chars;
        unsigned long value;
        char* target;

        if (is_space(*f)) {
            while (is_space(*f)) {
                f++;
            }
            while (is_space(c = (char)__StringRead(&in, 0, READ_GET))) {
                ++chars_read;
            }
            __StringRead(&in, c, READ_UNGET);
            continue;
        }
        if (*f != '%') {
            if ((c = __StringRead(&in, 0, READ_GET)) != (unsigned char)*f) {
                __StringRead(&in, c, READ_UNGET);
                break;
            }
            ++chars_read;
            ++f;
            continue;
        }
        ++f;
        if (*f == '*') {
            suppress = 1;
            ++f;
        }
        if (is_digit(*f)) {
            width = 0;
            while (is_digit(*f)) {
                width = width * 10 + (*f++ - '0');
            }
        }
        if (*f == 'h') {
            argument = scan_short;
            if (*++f == 'h') {
                argument = scan_char;
                ++f;
            }
        } else if (*f == 'l') {
            argument = scan_long;
            ++f;
        }
        c = *f++;
        target = !suppress && c != '%' ? va_arg(arg, char*) : 0;
        if (c != 'n' && __StringRead(&in, 0, READ_TEST)) {
            break;
        }
        switch (c) {
        case 'd':
            base = 10;
            goto convert;
        case 'i':
            base = 0;
            goto convert;
        case 'o':
            base = 8;
            goto convert;
        case 'u':
            base = 10;
            goto convert;
        case 'x':
        case 'X':
            base = 16;
        convert:
            value = __strtoul(base, width, __StringRead, &in, &num_chars, &negative, &overflow);
            if (!num_chars) {
                goto done;
            }
            chars_read += num_chars;
            if (negative) {
                value = -value;
            }
            if (target) {
                switch (argument) {
                case scan_normal:
                    *(int*)target = value;
                    break;
                case scan_char:
                    *(signed char*)target = value;
                    break;
                case scan_short:
                    *(short*)target = value;
                    break;
                case scan_long:
                    *(long*)target = value;
                    break;
                }
                ++items_assigned;
            }
            ++conversions;
            break;
        case 'n':
            if (target) {
                *(int*)target = chars_read;
            }
            break;
        default:
            goto done;
        }
    }
done:
    if (__StringRead(&in, 0, READ_TEST) && conversions == 0) {
        return -1;
    }
    return items_assigned;
}
