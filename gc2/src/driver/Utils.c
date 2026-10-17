#include "driver/Utils.h"
#include "driver/Arguments.h"
#include <stdio.h>
#include <string.h>

int my_tolower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c | 0x20;
    } else {
        return c;
    }
}

int my_isdigit(char c) {
    return (c >= '0' && c <= '9');
}

int my_isalpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int my_isalnum(char c) {
    return my_isdigit(c) || my_isalpha(c);
}

// (2.6 groups the two ranges; Pro7's flat `||` chain is 2 bytes longer)
int my_isxdigit(char c) {
    return my_isdigit(c) || ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
}

// TODO: invented names. 2.6's Utils_SpellList has locals Pro7's lacks, set and never read (two ints
// to 1 and two chars to 0 at its start, then `x = z = 0` after each prefix copy: real stores in the
// original, 0x420ca2..0x420cb8); x/y/z/c/d stand in for whatever they were (gc1 has the same stores,
// as an invented inline). Their registers say x is one variable throughout and y and z are two
// others. Pro7's text without them is 80%. The two loops are 2.6's too: Pro7's `for (cnt = npflen;
// cnt > 0; cnt--)` and `for (cnt = undo; cnt; cnt--)` each cost the match.
char* Utils_SpellList(char* list, char* buffer, char opts) {
    char* sptr;
    char* eptr;
    char* nptr;
    char* bptr;
    int pflen;
    int npflen;
    int undo;
    char* neptr;
    int cnt;
    int x;
    int y;
    int z;
    char c;
    char d;

    undo = 0;
    x = 1;
    y = 1;
    bptr = buffer;
    sptr = list;
    d = c = 0;
    pflen = 0;
    while (sptr) {
        if ((opts & 1) && !pflen) {
            *(bptr++) = *MAINOPTCHAR;
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
            if (opts & 8) {
                *(bptr++) = '[';
                *(bptr++) = 'n';
                *(bptr++) = 'o';
                *(bptr++) = ']';
            }
            if (opts & 0x20) {
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
                if ((neptr - nptr) <
                    (eptr - sptr) ||
                    ((sptr[1] && sptr[1] != '|') ? (sptr[1] != nptr[1]) : 0)) {
                    npflen = 0;
                }
                if (opts & 0x40) {
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

        if (opts & 0x10) {
            *(bptr++) = '[';
            *(bptr++) = '-';
            *(bptr++) = ']';
        }
        if (opts & 0x40) {
            *(bptr++) = '+';
        }

        sptr = nptr;
        if (nptr && bptr[-1] != '[') {
            if ((opts & 1) || (bptr[-1] == ']') || ((opts & 8) && !undo)) {
                *(bptr++) = ' ';
                *(bptr++) = '|';
                *(bptr++) = ' ';
            } else {
                *(bptr++) = '|';
            }
        }

        opts &= ~0x40;
        pflen = npflen;
        x = z = 0;
    }

    while (undo--) {
        *(bptr++) = ']';
    }

    if (opts & 4) {
        bptr += sprintf(bptr, "=...");
    }

    *bptr = 0;
    return bptr;
}

int Utils_CompareOptionString(const char* a, const char* b, int cased, int sticky) {
    const char* ae;
    const char* be;

    for (ae = a; *ae && *ae != '|'; ae++) {}
    for (be = b; *be && *be != '|'; be++) {}

    if (sticky && (be - b) < (ae - a)) {
        return 0;
    }

    if (cased) {
        while (a < ae && b < be) {
            if (*a != *b) {
                break;
            }
            a++;
            b++;
        }
    } else {
        while (a < ae && b < be) {
            if (my_tolower(*a) != my_tolower(*b)) {
                break;
            }
            a++;
            b++;
        }
    }

    return (a == ae) && (sticky || b == be);
}
