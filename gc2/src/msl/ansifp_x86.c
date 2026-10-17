#include "msl/ansifp_x86.h"
/*
 * MSL x86 decimal conversion: __scalenum, __dec2num and __num2dec from ansifp_x86.obj
 * (lib/msl/MSL_X86). The library ships this object without its source; this is a
 * reconstruction, byte-identical to the object apart from relocations. The library
 * was built by an older compiler than the rest of MSL: CodeWarrior Pro 4 with
 * `/O1` (optimize for size), which keeps every local an asm block names in memory.
 */

/* MSL ansi_fp.h, x86 (SIGDIGLEN 32) */

/* an x87 extended-precision value: its mantissa's 16-bit words, low first, then the sign and exponent */
typedef struct Extended80 {
    unsigned short mantissa[4];
    unsigned short exponent;
} Extended80;

/* 10^0 .. 10^31 */
static const Extended80 powers_of_ten[32] = {
    {{0x0000, 0x0000, 0x0000, 0x8000}, 0x3FFF},
    {{0x0000, 0x0000, 0x0000, 0xA000}, 0x4002},
    {{0x0000, 0x0000, 0x0000, 0xC800}, 0x4005},
    {{0x0000, 0x0000, 0x0000, 0xFA00}, 0x4008},
    {{0x0000, 0x0000, 0x0000, 0x9C40}, 0x400C},
    {{0x0000, 0x0000, 0x0000, 0xC350}, 0x400F},
    {{0x0000, 0x0000, 0x0000, 0xF424}, 0x4012},
    {{0x0000, 0x0000, 0x8000, 0x9896}, 0x4016},
    {{0x0000, 0x0000, 0x2000, 0xBEBC}, 0x4019},
    {{0x0000, 0x0000, 0x2800, 0xEE6B}, 0x401C},
    {{0x0000, 0x0000, 0xF900, 0x9502}, 0x4020},
    {{0x0000, 0x0000, 0xB740, 0xBA43}, 0x4023},
    {{0x0000, 0x0000, 0xA510, 0xE8D4}, 0x4026},
    {{0x0000, 0x0000, 0xE72A, 0x9184}, 0x402A},
    {{0x0000, 0x8000, 0x20F4, 0xB5E6}, 0x402D},
    {{0x0000, 0xA000, 0xA931, 0xE35F}, 0x4030},
    {{0x0000, 0x0400, 0xC9BF, 0x8E1B}, 0x4034},
    {{0x0000, 0xC500, 0xBC2E, 0xB1A2}, 0x4037},
    {{0x0000, 0x7640, 0x6B3A, 0xDE0B}, 0x403A},
    {{0x0000, 0x89E8, 0x2304, 0x8AC7}, 0x403E},
    {{0x0000, 0xAC62, 0xEBC5, 0xAD78}, 0x4041},
    {{0x8000, 0x177A, 0x26B7, 0xD8D7}, 0x4044},
    {{0x9000, 0x6EAC, 0x7832, 0x8786}, 0x4048},
    {{0xB400, 0x0A57, 0x163F, 0xA968}, 0x404B},
    {{0xA100, 0xCCED, 0x1BCE, 0xD3C2}, 0x404E},
    {{0x84A0, 0x4014, 0x5161, 0x8459}, 0x4052},
    {{0xA5C8, 0x9019, 0xA5B9, 0xA56F}, 0x4055},
    {{0x0F3A, 0xF420, 0x8F27, 0xCECB}, 0x4058},
    {{0x0984, 0xF894, 0x3978, 0x813F}, 0x405C},
    {{0x0BE5, 0x36B9, 0x07D7, 0xA18F}, 0x405F},
    {{0x4EDE, 0x0467, 0xC9CD, 0xC9F2}, 0x4062},
    {{0x2296, 0x4581, 0x7C40, 0xFC6F}, 0x4065},
};

/* 10^32, 10^64, 10^128, ... 10^(32 * 2^10) */
static const Extended80 big_powers_of_ten[11] = {
    {{0xB59E, 0x2B70, 0xADA8, 0x9DC5}, 0x4069},
    {{0xA6D5, 0xFFCF, 0x1F49, 0xC278}, 0x40D3},
    {{0x14A3, 0xC59B, 0xAB16, 0xEFB3}, 0x413D},
    {{0x8CE0, 0x80E9, 0x47C9, 0x93BA}, 0x41A8},
    {{0x17AB, 0x7FE6, 0xA12B, 0xB616}, 0x4212},
    {{0x556B, 0x3927, 0xF78D, 0xE070}, 0x427C},
    {{0xC930, 0xE33C, 0x96FF, 0x8A52}, 0x42E7},
    {{0xDE8E, 0x9DF9, 0xEBFB, 0xAA7E}, 0x4351},
    {{0x2F8D, 0x5C6A, 0xFC19, 0xD226}, 0x43BB},
    {{0xE376, 0xF2CC, 0x2F29, 0x8184}, 0x4426},
    {{0x0AD3, 0xDB90, 0x2700, 0x9FA4}, 0x4490},
};

/* MSL fenv.h's FE_DFL_ENV (its fenv_t is a long, the x87 control word, on x86) */
long _FE_DFL_ENV = 0x037F;

static long double *small_pow = (long double *)powers_of_ten;
static long double *big_pow = (long double *)big_powers_of_ten;
/* set: __scalenum divides by the power of ten */
static long scale_divide;

/* st(0) *= 10^eax; a negative exponent flips scale_divide */

static __declspec(naked) void __scalenum(void)
{
    asm
    {
        cmp     eax, 0
        jge     positive
        xor     scale_divide, 1
        neg     eax
    positive:
        fld1
        mov     ecx, eax
        mov     edx, eax
        cmp     ecx, 32
        jl      small
        and     eax, 0xffffffe0
        and     ecx, 0x1f
        fstp    st(0)
        sub     edx, eax
        shr     eax, 5
        dec     eax
        imul    eax, eax, 10
        mov     edx, big_pow
        add     edx, eax
        fld     tbyte ptr [edx]
    small:
        mov     edx, small_pow
        imul    ecx, ecx, 10
        add     edx, ecx
        fld     tbyte ptr [edx]
        fmulp   st(1), st
        cmp     scale_divide, 0
        jne     divide
        fmulp   st(1), st
        jmp     done
    divide:
        fdivp   st(1), st
    done:
        ret
    }
}

double __dec2num(const decimal *d)
{
    unsigned char bcd[10];
    double x = 0.0;
    int i;
    short cw;
    unsigned char *p = bcd;
    unsigned char b;

    /* the packed BCD operand of fbld: 18 digits, least significant pair first */
    for (i = 0; i < 9; i++) {
        b = d->sig.text[2 * i] << 4;
        bcd[9 - (i + 1)] = b | (d->sig.text[2 * i + 1] & 0xf);
    }
    bcd[9] = d->sgn;
    i = d->exp - (18 - d->sig.length);
    scale_divide = 0;
    asm
    {
        fnstcw  cw
        or      cw, 0x300
        fldcw   cw
        mov     edx, p
        fbld    tbyte ptr [edx]
        mov     eax, i
        call    __scalenum
        fstp    x
    }
    return x;
}

#define __HI(x) (*(1 + (long *)&(x)))
#define __LO(x) (*(unsigned long *)&(x))
static inline int __fpclassifyd(double x)
{
 switch (__HI(x) & 0x7ff00000) {
 case 0x7ff00000:
 if ((__HI(x) & 0x000fffff) || __LO(x)) { return 1;
 }
 return 2;
 case 0:
 if ((__HI(x) & 0x000fffff) || __LO(x)) { return 5;
 }
 return 3;
 default: return 4;
 }
}
static inline int __signbitd(double x) { return __HI(x) & 0x80000000; }
void __num2dec(const decform *f, double x, decimal *d)
{
 short cw = 0x37f;
 float bias = 17.0f;
 short savecw;
 double big = 1.0e17;
 double ten = 10.0;
 unsigned char *text = d->sig.text;
 d->sgn = (char)(__signbitd(x) >> 31);
 switch (__fpclassifyd(x)) {
 case 1: d->sig.text[0] = 'N'; d->sig.length = 1; return;
 case 2: d->sig.text[0] = 'I'; d->sig.length = 1; return;
 case 3: d->sig.text[0] = '0'; d->sig.length = 1; return;
 case 4:
 case 5:
 scale_divide = 1;
 asm {
 fnstcw savecw
 fldcw cw
 fld qword ptr x
 fabs
 fld st(0)
 fxtract
 fxch st(1)
 fstp st(1)
 fldlg2
 fmulp st(1), st
 fsub bias
 mov eax, d
 fistp dword ptr [eax + 2]
 mov eax, dword ptr [eax + 2]
 call __scalenum
 fld big
 fcomp st(1)
 fnstsw ax
 sahf
 jbe skip
 fmul ten
 mov eax, d
 dec dword ptr [eax + 2]
skip:
 sub esp, 14
 mov eax, text
 fbstp tbyte ptr [esp + 4]
 mov ecx, 9
next:
 dec ecx
 mov dl, byte ptr [esp + ecx + 4]
 mov dh, dl
 shr dl, 4
 and dx, 0x0f0f
 or dx, 0x3030
 mov word ptr [eax], dx
 add eax, 2
 jecxz done
 jmp next
done:
 add esp, 14
 fldcw savecw
 }
 default: d->sig.length = 18;
 }
}
