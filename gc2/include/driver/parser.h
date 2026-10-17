#ifndef DRIVER_PARSER_H
#define DRIVER_PARSER_H

#include "compiler/common.h"

// The parser plugin's types (Pro7's parser.h); ParserTool and PrefDataPanel are defined in
// ParserFace.h
typedef struct ParserTool ParserTool;
typedef struct PrefDataPanel PrefDataPanel;

// Pragma.flags: an on/off setting, or one whose sense is reversed
enum {
    PRAGMA_FLAGS_0,
    PRAGMA_FLAGS_1
};

// Pro7's pragma settings (parser.h): a byte-sized enum, so `set ? PR_ON : PR_OFF`
// is a byte expression
enum {
    PR_UNSET = 0,
    PR_ON = 1,
    PR_OFF = 2,
    PR_AUTO = 3,
    PR_RESET = 4
};

// A pragma the parser turns a setting into (Pro7's assumed name; SetupPragmas writes them)
typedef struct Pragma {
    void* value;
    const char* pragma;
    int flags;
} Pragma;

#endif
