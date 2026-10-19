/* MSL's x86 decimal conversion (gc2's src/msl/ansifp_x86.c) without the x87: __dec2num and __num2dec on a software
 * extended-precision number that rounds as the x87 does under the control words they set (64-bit significands,
 * round to nearest even), so each gives the bits the original gives, double rounding included. */
#include "msl/ansifp_x86.h"

typedef unsigned long long u64;
typedef unsigned int u32;

/* sign * m * 2^e; m's top bit is set unless the value is zero (m == 0) */
typedef struct Ext {
    int sign;
    int e;
    u64 m;
} Ext;

/* the x87's 80-bit tables (ansifp_x86.c's): the mantissa's 16-bit words, low first, then sign and exponent */
typedef struct Extended80 {
    unsigned short mantissa[4];
    unsigned short exponent;
} Extended80;

static const Extended80 powers_of_ten[32] = {
    {{0x0000, 0x0000, 0x0000, 0x8000}, 0x3FFF}, {{0x0000, 0x0000, 0x0000, 0xA000}, 0x4002},
    {{0x0000, 0x0000, 0x0000, 0xC800}, 0x4005}, {{0x0000, 0x0000, 0x0000, 0xFA00}, 0x4008},
    {{0x0000, 0x0000, 0x0000, 0x9C40}, 0x400C}, {{0x0000, 0x0000, 0x0000, 0xC350}, 0x400F},
    {{0x0000, 0x0000, 0x0000, 0xF424}, 0x4012}, {{0x0000, 0x0000, 0x8000, 0x9896}, 0x4016},
    {{0x0000, 0x0000, 0x2000, 0xBEBC}, 0x4019}, {{0x0000, 0x0000, 0x2800, 0xEE6B}, 0x401C},
    {{0x0000, 0x0000, 0xF900, 0x9502}, 0x4020}, {{0x0000, 0x0000, 0xB740, 0xBA43}, 0x4023},
    {{0x0000, 0x0000, 0xA510, 0xE8D4}, 0x4026}, {{0x0000, 0x0000, 0xE72A, 0x9184}, 0x402A},
    {{0x0000, 0x8000, 0x20F4, 0xB5E6}, 0x402D}, {{0x0000, 0xA000, 0xA931, 0xE35F}, 0x4030},
    {{0x0000, 0x0400, 0xC9BF, 0x8E1B}, 0x4034}, {{0x0000, 0xC500, 0xBC2E, 0xB1A2}, 0x4037},
    {{0x0000, 0x7640, 0x6B3A, 0xDE0B}, 0x403A}, {{0x0000, 0x89E8, 0x2304, 0x8AC7}, 0x403E},
    {{0x0000, 0xAC62, 0xEBC5, 0xAD78}, 0x4041}, {{0x8000, 0x177A, 0x26B7, 0xD8D7}, 0x4044},
    {{0x9000, 0x6EAC, 0x7832, 0x8786}, 0x4048}, {{0xB400, 0x0A57, 0x163F, 0xA968}, 0x404B},
    {{0xA100, 0xCCED, 0x1BCE, 0xD3C2}, 0x404E}, {{0x84A0, 0x4014, 0x5161, 0x8459}, 0x4052},
    {{0xA5C8, 0x9019, 0xA5B9, 0xA56F}, 0x4055}, {{0x0F3A, 0xF420, 0x8F27, 0xCECB}, 0x4058},
    {{0x0984, 0xF894, 0x3978, 0x813F}, 0x405C}, {{0x0BE5, 0x36B9, 0x07D7, 0xA18F}, 0x405F},
    {{0x4EDE, 0x0467, 0xC9CD, 0xC9F2}, 0x4062}, {{0x2296, 0x4581, 0x7C40, 0xFC6F}, 0x4065},
};

static const Extended80 big_powers_of_ten[11] = {
    {{0xB59E, 0x2B70, 0xADA8, 0x9DC5}, 0x4069}, {{0xA6D5, 0xFFCF, 0x1F49, 0xC278}, 0x40D3},
    {{0x14A3, 0xC59B, 0xAB16, 0xEFB3}, 0x413D}, {{0x8CE0, 0x80E9, 0x47C9, 0x93BA}, 0x41A8},
    {{0x17AB, 0x7FE6, 0xA12B, 0xB616}, 0x4212}, {{0x556B, 0x3927, 0xF78D, 0xE070}, 0x427C},
    {{0xC930, 0xE33C, 0x96FF, 0x8A52}, 0x42E7}, {{0xDE8E, 0x9DF9, 0xEBFB, 0xAA7E}, 0x4351},
    {{0x2F8D, 0x5C6A, 0xFC19, 0xD226}, 0x43BB}, {{0xE376, 0xF2CC, 0x2F29, 0x8184}, 0x4426},
    {{0x0AD3, 0xDB90, 0x2700, 0x9FA4}, 0x4490},
};

/* MSL fenv.h's FE_DFL_ENV (its fenv_t is a long, the x87 control word, on x86) */
long _FE_DFL_ENV = 0x037F;

static Ext from80(const Extended80* x)
{
    Ext r;
    r.m = (u64)x->mantissa[0] | (u64)x->mantissa[1] << 16 | (u64)x->mantissa[2] << 32 | (u64)x->mantissa[3] << 48;
    r.sign = x->exponent >> 15;
    r.e = (int)(x->exponent & 0x7FFF) - 16383 - 63;
    return r;
}

static int top_bit(u64 x)
{
    int n = 63;
    while (!(x >> n)) {
        n--;
    }
    return n;
}

/* sign * (r2:r1:r0) * 2^e, STICKY for anything nonzero below r0, rounded to a 64-bit significand (to nearest, ties
 * to even) */
static Ext round192(int sign, int e, u64 r2, u64 r1, u64 r0, int sticky)
{
    Ext r;
    u64 round, rest;
    r.sign = sign;
    r.e = 0;
    r.m = 0;
    if (!r2 && !r1 && !r0) {
        return r;
    }
    while (!r2) {
        r2 = r1;
        r1 = r0;
        r0 = 0;
        e -= 64;
    }
    if (!(r2 >> 63)) {
        int shift = 63 - top_bit(r2);
        r2 = r2 << shift | r1 >> (64 - shift);
        r1 = r1 << shift | r0 >> (64 - shift);
        r0 <<= shift;
        e -= shift;
    }
    round = r1 >> 63;
    rest = (r1 << 1) | r0 | (u64)(sticky != 0);
    r.m = r2;
    r.e = e + 128;
    if (round && (rest || (r2 & 1))) {
        r.m++;
        if (!r.m) {
            r.m = 1ULL << 63;
            r.e++;
        }
    }
    return r;
}

static void mul64(u64 a, u64 b, u64* hi, u64* lo)
{
    u64 a0 = (u32)a, a1 = a >> 32, b0 = (u32)b, b1 = b >> 32;
    u64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    u64 middle = (p00 >> 32) + (u32)p01 + (u32)p10;
    *lo = (middle << 32) | (u32)p00;
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);
}

/* fmul */
static Ext mul(Ext a, Ext b)
{
    u64 hi, lo;
    if (!a.m || !b.m) {
        Ext z = {a.sign ^ b.sign, 0, 0};
        return z;
    }
    mul64(a.m, b.m, &hi, &lo);
    return round192(a.sign ^ b.sign, a.e + b.e, 0, hi, lo, 0);
}

/* fdiv */
static Ext divide_ext(Ext a, Ext b)
{
    /* the quotient a.m / b.m, in [0.5, 2), to 67 bits by restoring division; the remainder is sticky */
    u64 rem = a.m, qhi = 0, qlo = 0;
    int carry = 0, i;
    if (!a.m) {
        Ext z = {a.sign ^ b.sign, 0, 0};
        return z;
    }
    for (i = 0; i <= 66; i++) {
        qhi = qhi << 1 | qlo >> 63;
        qlo <<= 1;
        if (carry || rem >= b.m) {
            rem -= b.m;
            qlo |= 1;
        }
        carry = (int)(rem >> 63);
        rem <<= 1;
    }
    return round192(a.sign ^ b.sign, a.e - b.e - 66, 0, qhi, qlo, rem != 0 || carry);
}

/* fadd (fsub is an add of the negation) */
static Ext add(Ext a, Ext b)
{
    u64 b2, b1, b0, r2, r1, r0;
    int d, sticky = 0;
    Ext t;
    if (!b.m) {
        return a;
    }
    if (!a.m) {
        return b;
    }
    if (a.e < b.e || (a.e == b.e && a.m < b.m)) {
        t = a;
        a = b;
        b = t;
    }
    d = a.e - b.e;
    /* b at a's scale, as 192 bits (a.m being the top 64): exact below 128 bits of shift, else only sticky */
    if (d >= 128) {
        b2 = b1 = b0 = 0;
        sticky = 1;
    } else if (d >= 64) {
        b2 = 0;
        b1 = b.m >> (d - 64);
        b0 = d > 64 ? b.m << (128 - d) : 0;
    } else {
        b2 = b.m >> d;
        b1 = d ? b.m << (64 - d) : 0;
        b0 = 0;
    }
    if (a.sign == b.sign) {
        r0 = b0;
        r1 = b1;
        r2 = a.m + b2;
        if (r2 < a.m) {
            /* the carry out: one bit more, the lowest one sticky */
            sticky |= (int)(r0 & 1);
            r0 = r0 >> 1 | r1 << 63;
            r1 = r1 >> 1 | r2 << 63;
            r2 = r2 >> 1 | 1ULL << 63;
            return round192(a.sign, a.e - 128 + 1, r2, r1, r0, sticky);
        }
        return round192(a.sign, a.e - 128, r2, r1, r0, sticky);
    }
    if (sticky) {
        /* a less something below all of the 192 bits: one less at the bottom, the rest sticky */
        b0 = 1;
    }
    r0 = 0 - b0;
    r1 = 0 - b1 - (b0 != 0);
    r2 = a.m - b2 - (b1 != 0 || b0 != 0);
    return round192(a.sign, a.e - 128, r2, r1, r0, sticky);
}

static Ext from_int(long long v)
{
    Ext r;
    r.sign = v < 0;
    r.m = (u64)(v < 0 ? -v : v);
    r.e = 0;
    if (r.m) {
        int s = 63 - top_bit(r.m);
        r.m <<= s;
        r.e = -s;
    }
    return r;
}

static Ext from_double(double x)
{
    union {
        double d;
        u64 u;
    } v;
    Ext r;
    int exponent;
    v.d = x;
    r.sign = (int)(v.u >> 63);
    exponent = (int)(v.u >> 52 & 0x7FF);
    r.m = v.u & ((1ULL << 52) - 1);
    if (exponent) {
        r.m |= 1ULL << 52;
        r.e = exponent - 1075;
    } else {
        r.e = -1074;
    }
    if (r.m) {
        int s = 63 - top_bit(r.m);
        r.m <<= s;
        r.e -= s;
    }
    return r;
}

/* fstp qword: to a double, rounded to nearest (ties to even), subnormal where it is that small */
static double to_double(Ext x)
{
    union {
        double d;
        u64 u;
    } v;
    int exponent, keep;
    u64 m, round, rest;
    if (!x.m) {
        v.u = (u64)x.sign << 63;
        return v.d;
    }
    exponent = x.e + 63; /* the value is in [2^exponent, 2^(exponent + 1)) */
    keep = exponent >= -1022 ? 53 : 53 - (-1022 - exponent);
    if (keep <= 0) {
        /* below half the smallest subnormal, or at it (rounding to it only above) */
        m = 0;
        if (keep == 0 && x.m > 1ULL << 63) {
            m = 1;
        }
        v.u = (u64)x.sign << 63 | m;
        return v.d;
    }
    m = x.m >> (64 - keep);
    round = x.m >> (63 - keep) & 1;
    rest = x.m << (keep + 1);
    if (round && (rest || (m & 1))) {
        m++;
        if (m >> keep) {
            m >>= 1;
            exponent++;
            if (keep < 53) {
                keep++;
                m <<= 1;
            }
        }
    }
    if (exponent > 1023) {
        v.u = (u64)x.sign << 63 | 0x7FFULL << 52;
    } else if (exponent >= -1022) {
        v.u = (u64)x.sign << 63 | (u64)(exponent + 1023) << 52 | (m & ((1ULL << 52) - 1));
    } else {
        v.u = (u64)x.sign << 63 | m;
    }
    return v.d;
}

/* the integer nearest X's magnitude (ties to even), as fistp and fbstp round; whether it is below 2^BITS */
static u64 to_integer(Ext x, int bits, int* fits)
{
    u64 n, round, rest;
    int shift;
    *fits = 1;
    if (!x.m || x.e < -64) {
        return 0; /* below 2^-1: zero */
    }
    if (x.e >= 0) {
        *fits = x.e == 0 && 63 < bits;
        return x.e < 64 ? x.m << x.e : 0;
    }
    shift = -x.e;
    if (shift == 64) {
        n = 0;
        round = 1;
        rest = x.m << 1;
    } else {
        n = x.m >> shift;
        round = x.m >> (shift - 1) & 1;
        rest = shift == 1 ? 0 : x.m << (65 - shift);
    }
    if (round && (rest || (n & 1))) {
        n++;
    }
    *fits = n == 0 || top_bit(n) < bits;
    return n;
}

/* a < b, for magnitudes */
static int less(Ext a, Ext b)
{
    if (!a.m || !b.m) {
        return !a.m && b.m;
    }
    return a.e != b.e ? a.e < b.e : a.m < b.m;
}

/* __scalenum: X * 10^n, or X / 10^n for a negative n; DIVIDE flips the two */
static Ext scale(Ext x, int n, int divide)
{
    Ext power;
    int small = n, big;
    if (n < 0) {
        divide ^= 1;
        n = -n;
        small = n;
    }
    if (n >= 32) {
        big = (n >> 5) - 1;
        small = n & 31;
        if (big > 10) {
            /* (past the original's table, from 10^384 on: it reads what follows it, which this does not have; a
               literal's value can differ there, see docs/wasm.md) */
            big = 10;
        }
        power = mul(from80(&big_powers_of_ten[big]), from80(&powers_of_ten[small]));
    } else {
        power = from80(&powers_of_ten[small]);
    }
    return divide ? divide_ext(x, power) : mul(x, power);
}

/* Set once __dec2num has run: its `or cw, 0x300` leaves the x87 at 64-bit precision for the rest of the run, where it
 * was at 53 (Windows's default). */
int __x87_extended;

double __dec2num(const decimal* d)
{
    /* fbld: the 18 digits as one integer (the sign is bit 7 of the sign byte, which is d->sgn) */
    u64 digits = 0;
    int i;
    Ext x;
    for (i = 0; i < 18; i++) {
        digits = digits * 10 + (d->sig.text[i] & 0xF);
    }
    x = from_int((long long)digits);
    x.sign = (d->sgn & 0x80) != 0;
    i = d->exp - (18 - d->sig.length);
    __x87_extended = 1;
    return to_double(scale(x, i, 0));
}

/* A OP B ('+', '-', '*' or '/') as the original computes a double's: on the x87, rounded to its precision and then
 * to a double where it is stored. At 64-bit precision that is twice; a zero, infinite or NaN result (or operand) is
 * the same either way. */
double __x87_double(int op, double a, double b)
{
    double r = op == '+' ? a + b : op == '-' ? a - b : op == '*' ? a * b : a / b;
    Ext x, y;
    if (!__x87_extended || r == 0 || r - r != 0 || a - a != 0 || b - b != 0 || a == 0 || b == 0) {
        return r;
    }
    x = from_double(a);
    y = from_double(b);
    switch (op) {
    case '-':
        y.sign ^= 1;
        /* fall through */
    case '+':
        return to_double(add(x, y));
    case '*':
        return to_double(mul(x, y));
    default:
        return to_double(divide_ext(x, y));
    }
}

static int __fpclassifyd(double x)
{
    union {
        double d;
        u64 u;
    } v;
    u32 hi, lo;
    v.d = x;
    hi = (u32)(v.u >> 32);
    lo = (u32)v.u;
    switch (hi & 0x7ff00000) {
    case 0x7ff00000:
        return (hi & 0x000fffff) || lo ? 1 : 2;
    case 0:
        return (hi & 0x000fffff) || lo ? 5 : 3;
    default:
        return 4;
    }
}

void __num2dec(const decform* f, double x, decimal* d)
{
    /* log10(2) as fldlg2 rounds it to nearest: 0x3FFD 9A209A84FBCFF799 */
    static const Extended80 lg2 = {{0xF799, 0xFBCF, 0x9A84, 0x9A20}, 0x3FFD};
    unsigned char* text = d->sig.text;
    Ext y, k;
    int fits, n, i;
    u64 value;
    union {
        double d;
        u64 u;
    } v;
    v.d = x;
    /* (the sign bit shifted arithmetically: -1 for a negative x) */
    d->sgn = (char)-(int)(v.u >> 63);
    switch (__fpclassifyd(x)) {
    case 1:
        d->sig.text[0] = 'N';
        d->sig.length = 1;
        return;
    case 2:
        d->sig.text[0] = 'I';
        d->sig.length = 1;
        return;
    case 3:
        d->sig.text[0] = '0';
        d->sig.length = 1;
        return;
    case 4:
    case 5:
        y = from_double(x);
        y.sign = 0;
        /* fxtract's exponent times log10(2), less 17, rounded to an integer: the power of ten to divide by */
        k = mul(from_int(y.e + 63), from80(&lg2));
        k = add(k, from_int(-17));
        value = to_integer(k, 31, &fits);
        n = !fits ? (int)0x80000000 : k.sign ? -(int)value : (int)value;
        /* (fistp stores the dword over exp and the two bytes after it) */
        d->exp = (short)n;
        d->sig.length = (unsigned char)((unsigned)n >> 16);
        d->sig.text[0] = (unsigned char)((unsigned)n >> 24);
        y = scale(y, n, 1);
        if (less(y, from_int(100000000000000000LL))) {
            y = mul(y, from_int(10));
            d->exp--;
        }
        /* fbstp: the 18 digits, or packed BCD's indefinite where the integer has more */
        value = to_integer(y, 64, &fits);
        if (!fits || value >= 1000000000000000000ULL) {
            /* bytes 8..0: 0xFF, 0xC0, then zeros (byte 9, 0xFF, is the sign byte) */
            static const unsigned char indefinite[9] = {0, 0, 0, 0, 0, 0, 0, 0xC0, 0xFF};
            for (i = 8; i >= 0; i--) {
                *text++ = (indefinite[i] >> 4 & 0x0F) | 0x30;
                *text++ = (indefinite[i] & 0x0F) | 0x30;
            }
        } else {
            char digits[18];
            for (i = 17; i >= 0; i--) {
                digits[i] = (char)('0' + value % 10);
                value /= 10;
            }
            for (i = 0; i < 18; i++) {
                text[i] = (unsigned char)digits[i];
            }
        }
    default:
        d->sig.length = 18;
    }
}
