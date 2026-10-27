#define CERROR_FILE "unknown.c"
#include "compiler/common.h"
#include "driver/Utils.h"
#include "driver/Targets.h"
#include <string.h>
#include <stdio.h>
int to_lowercase(int character)
{
    if ((char)character >= 'A' && (char)character <= 'Z')
        return (char)character | 32;
    return (char)character;
}

unsigned int Utils_IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

int Utils_IsAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int Utils_IsAlnum(char character)
{
    unsigned int firstResult;
    int secondResult;
    int result;

    result = 1;
    firstResult = Utils_IsDigit(character);
    if (firstResult == 0) {
        secondResult = Utils_IsAlpha(character);
        if (secondResult == 0) {
            result = 0;
        }
    }
    return result;
}

int Utils_IsHexDigit(char c)
{
    return Utils_IsDigit(c) || ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
}

static inline void resetflags(void)
{
    int a, b;
    a = b = 0;
}

static inline char *putbar(char *out)
{
    *out = '|';
    return out + 1;
}

#if VERSION >= VERSION_GC_2_7
/* (the 2.x driver's, as mweppc has it: its flags are 1 the option character, 8 a [no] prefix, 0x20 a [no-] prefix,
   0x10 a [-] suffix and 0x40 sticky) */
char *Utils_FormatOptions(char *options, char *destination, SInt8 flags)
{
    char *sptr;
    char *eptr;
    char *nptr;
    char *bptr;
    int pflen;
    int npflen;
    int undo;
    char *neptr;
    int cnt;
    int x;
    int y;
    int z;
    char c;
    char d;

    undo = 0;
    x = 1;
    y = 1;
    bptr = destination;
    sptr = options;
    d = c = 0;
    pflen = 0;
    while (sptr) {
        if ((flags & 1) && !pflen) {
            *(bptr++) = *data_00587eec;
        }

        eptr = strchr(sptr, '|');
        if (!eptr) {
            eptr = sptr + strlen(sptr);
            nptr = 0;
        } else if (eptr[1] == '|') {
            nptr = 0;
        } else {
            nptr = eptr + 1;
        }

        if (undo == 0 && !pflen) {
            if (flags & 8) {
                *(bptr++) = '[';
                *(bptr++) = 'n';
                *(bptr++) = 'o';
                *(bptr++) = ']';
            }
            if (flags & 0x20) {
                *(bptr++) = '[';
                *(bptr++) = 'n';
                *(bptr++) = 'o';
                *(bptr++) = '-';
                *(bptr++) = ']';
            }
        }

        npflen = 0;
        if (nptr) {
            while (sptr < nptr && *nptr && *nptr != '|' && sptr[npflen] == nptr[npflen]) {
                npflen++;
            }

            if (npflen) {
                neptr = strchr(nptr, '|');
                if (!neptr) {
                    neptr = nptr + strlen(nptr);
                }
                if ((neptr - nptr) < (eptr - sptr) || ((sptr[1] && sptr[1] != '|') ? (sptr[1] != nptr[1]) : 0)) {
                    npflen = 0;
                }
                if (flags & 0x40) {
                    npflen = 0;
                }
            }
        }

        if (pflen) {
            x = z = 0;
            sptr += pflen;
            while (sptr < eptr) {
                *(bptr++) = *(sptr++);
            }
            if (npflen > pflen) {
                *(bptr++) = '[';
                undo++;
            }
            if (npflen < pflen) {
                *(bptr++) = ']';
                undo--;
            }
        } else if (npflen) {
            for (cnt = 0; cnt++ < npflen;) {
                *(bptr++) = *(sptr++);
            }
            *(bptr++) = '[';
            undo++;
        }

        while (sptr < eptr) {
            *(bptr++) = *(sptr++);
        }

        if (flags & 0x10) {
            *(bptr++) = '[';
            *(bptr++) = '-';
            *(bptr++) = ']';
        }
        if (flags & 0x40) {
            *(bptr++) = '+';
        }

        sptr = nptr;
        if (nptr && bptr[-1] != '[') {
            if ((flags & 1) || (bptr[-1] == ']') || ((flags & 8) && !undo)) {
                *(bptr++) = ' ';
                *(bptr++) = '|';
                *(bptr++) = ' ';
            } else {
                *(bptr++) = '|';
            }
        }

        flags &= ~0x40;
        pflen = npflen;
        x = z = 0;
    }

    while (undo--) {
        *(bptr++) = ']';
    }

    if (flags & 4) {
        bptr += sprintf(bptr, "=...");
    }

    *bptr = 0;
    return bptr;
}
#else
char *Utils_FormatOptions(char *options, char *destination, SInt8 flags)
{
    char *option;
    char *separator;
    char *nextOption;
    int previousPrefix;
    int commonPrefix;
    int bracketDepth;
    int suffixLength;
    char c;
    char d;
    int prefixIndex;
    char *out;

    bracketDepth = 0;
    d = 1;
    suffixLength = 1;
    out = destination;
    option = options;
    d = c = 0;
    previousPrefix = 0;
    while (option != NULL) {
        if ((flags & 1) != 0 && previousPrefix == 0) {
            *out++ = *data_00587eec;
        }
        separator = strchr(option, '|');
        if (separator == NULL) {
            separator = option + strlen(option);
            nextOption = NULL;
        } else if (separator[1] == '|') {
            nextOption = NULL;
        } else {
            nextOption = separator + 1;
        }
        if (bracketDepth == 0 && previousPrefix == 0) {
            if ((flags & 8) != 0) {
                *out = '[';
                out = out + 1;
                *out = 'n';
                out = out + 1;
                *out = 'o';
                out = out + 1;
                *out = ']';
                out = out + 1;
            }
            if ((flags & 0x20) != 0) {
                *out = '[';
                out = out + 1;
                *out = 'n';
                out = out + 1;
                *out = 'o';
                out = out + 1;
                *out = '-';
                out = out + 1;
                *out = ']';
                out = out + 1;
            }
        }
        commonPrefix = 0;
        if (nextOption != NULL) {
            for (; option < nextOption && *nextOption != '\0' && *nextOption != '|' &&
                   option[commonPrefix] == nextOption[commonPrefix];
                 ++commonPrefix) {
            }
            if (commonPrefix != 0) {
                char *nextSeparator = strchr(nextOption, '|');
                if (nextSeparator == NULL) {
                    nextSeparator = nextOption + strlen(nextOption);
                }
                if (nextSeparator - nextOption < separator - option ||
                    ((option[1] != '\0' && option[1] != '|') ? (int)(option[1] != nextOption[1]) : 0)) {
                    commonPrefix = 0;
                }
                if ((flags & 0x40) != 0) {
                    commonPrefix = 0;
                }
            }
        }
        if (previousPrefix != 0) {
            resetflags();
            option += previousPrefix;
            while (option < separator) {
                *out++ = *option++;
            }
            if (commonPrefix > previousPrefix) {
                *out++ = '[';
                ++bracketDepth;
            }
            if (commonPrefix < previousPrefix) {
                *out++ = ']';
                --bracketDepth;
            }
        } else {
            if (commonPrefix != 0) {
                for (prefixIndex = 0; prefixIndex++ < commonPrefix;) {
                    *out++ = *option++;
                }
                *out++ = '[';
                ++bracketDepth;
            }
        }
        while (option < separator) {
            *out++ = *option++;
        }
        if ((flags & 0x10) != 0) {
            *out++ = '[';
            *out++ = '-';
            *out++ = ']';
        }
        if ((flags & 0x40) != 0) {
            *out++ = '+';
        }
        option = nextOption;
        if (nextOption != NULL && out[-1] != '[') {
            if ((flags & 1) != 0 || out[-1] == ']' || ((flags & 8) != 0 && bracketDepth == 0)) {
                *out++ = ' ';
                *out++ = '|';
                *out++ = ' ';
            } else {
                out = putbar(out);
            }
        }
        flags &= 0xbf;
        previousPrefix = commonPrefix;
        resetflags();
    }
    while (bracketDepth--) {
        *out++ = ']';
    }
    if ((flags & 4) != 0) {
        suffixLength = sprintf(out, "=...");
        out += suffixLength;
    }
    *out = '\0';
    return out;
}
#endif

int Utils_MatchStringSegments(char *segment, char *candidate, int caseSensitive, int allowAbbreviation)
{
    char *segmentEnd;
    char *candidateEnd;
    for (segmentEnd = segment; *segmentEnd && *segmentEnd != '|'; segmentEnd++)
        ;
    for (candidateEnd = candidate; *candidateEnd && *candidateEnd != '|'; candidateEnd++)
        ;
    if (allowAbbreviation && candidateEnd - candidate < segmentEnd - segment)
        return 0;
    if (caseSensitive) {
        while (segment < segmentEnd && candidate < candidateEnd) {
            if (*segment != *candidate)
                break;
            segment++;
            candidate++;
        }
    } else {
        while (segment < segmentEnd && candidate < candidateEnd) {
            if (TO_LOWERCASE_CHAR(*segment) != TO_LOWERCASE_CHAR(*candidate))
                break;
            segment++;
            candidate++;
        }
    }
    return segment == segmentEnd && (allowAbbreviation || candidate == candidateEnd);
}
