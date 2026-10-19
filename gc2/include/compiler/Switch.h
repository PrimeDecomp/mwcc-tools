#ifndef COMPILER_SWITCH_H
#define COMPILER_SWITCH_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// 2.6's case node is Pro7's without `max` (16 bytes: CInline_UnpackSwitch allocates that much)
struct SwitchCase {
    struct SwitchCase* next;
    struct CLabel* label;
    CInt64 min;
};

struct SwitchInfo {
    struct SwitchCase* cases;
    struct CLabel* defaultlabel;
    // TODO: invented name (Pro7: x8): the switch expression's type, which the case values are
    // converted to
    Type* exprtype;
};

extern ObjectList* switchtables;

extern void switchstatement(ENode* expr, struct SwitchInfo* info);
extern void dumpswitchtables(Object* funcobj);

#ifdef __cplusplus
}
#endif

#endif
