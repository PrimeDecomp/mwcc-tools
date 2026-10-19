#include "compiler/UseDefChains.h"
#include "compiler/Alias.h"
#include "compiler/BitVectors.h"
#include "compiler/CompilerTools.h"
#include "compiler/Registers.h"

int number_of_Defs;
UseOrDef* Defs;
RegUseOrDef** reg_Defs[RegClassMax];
int number_of_Uses;
UseOrDef* Uses;
RegUseOrDef** reg_Uses[RegClassMax];
UseDefInfo* usedefinfo;
ObjectUseDef* objectusedefs;
ObjectUseDef* objectusedeflist;
#if VERSION >= VERSION_GC_2_5
// 2.5: used_virtual_registers when the chains were computed (Wii's name)
SInt32 last_usedef_register[RegClassMax];
#endif

ObjectUseDef* findobjectusedef(Object* object) {
    ObjectUseDef* oud;

    oud = objectusedefs;
    while (oud) {
        if (object < oud->object) {
            oud = oud->leftchild;
        } else if (object > oud->object) {
            oud = oud->rightchild;
        } else {
            return oud;
        }
    }

    return NULL;
}

#if VERSION < VERSION_GC_2_5
static void addobjectusedef(Object* object) {
    ObjectUseDef** ptr;
    ObjectUseDef* list;

    ptr = &objectusedefs;
    while (*ptr) {
        if (object < (*ptr)->object) {
            ptr = &(*ptr)->leftchild;
        } else if (object > (*ptr)->object) {
            ptr = &(*ptr)->rightchild;
        } else {
            return;
        }
    }

    list = oalloc(sizeof(ObjectUseDef));
    list->leftchild = list->rightchild = NULL;
    list->object = object;
    list->uses = list->defs = NULL;
    list->next = objectusedeflist;
    objectusedeflist = list;
    *ptr = list;
}
#else
// `inline` (Pro7: plain static): 2.6 inlines it into both callers, which plain -inline auto does not
static inline void addobjectusedef(Object* object) {
    ObjectUseDef* list;
    ObjectUseDef** ptr;

    ptr = &objectusedefs;
    while ((list = *ptr)) {
        if (object < list->object) {
            ptr = &list->leftchild;
        } else if (object > list->object) {
            ptr = &list->rightchild;
        } else {
            return;
        }
    }

    list = oalloc(sizeof(ObjectUseDef));
    memclrw(list, sizeof(ObjectUseDef));
    list->object = object;
    list->next = objectusedeflist;
    objectusedeflist = list;
    *ptr = list;
}
#endif

static void addaliasusedef(Alias* alias) {
    AliasMember* member;

#if VERSION < VERSION_GC_2_5
    if (alias && alias != worst_case_ptr) {
#else
    if (alias && alias != worst_case_ptr && alias != worst_case_func) {
#endif
        if (alias->type == AliasType_Set) {
            for (member = alias->parents; member; member = member->nextParent) {
                addaliasusedef(member->child);
            }
        } else {
            addobjectusedef(alias->object);
        }
    }
}

static void addworstcase(void) {
    addobjectusedef(&worst_case_obj);
}

static void precomputememoryusedefs(void) {
    PCodeBlock* block;
    PCode* pcode;

    objectusedefs = objectusedeflist = NULL;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (pcode->flags & (fIsRead | fIsWrite)) {
                if (pcode->alias) {
#if VERSION < VERSION_GC_2_5
                    if (pcode->alias->type ==
                        AliasType_Set ||
                        pcode->alias->size != nbytes_loaded_or_stored_by(pcode)) {
#else
                    if ((pcode->alias->type !=
                         AliasType_ObjectPart &&
                         pcode->alias->type != AliasType_Object) ||
                        pcode->alias->size != nbytes_loaded_or_stored_by(pcode)) {
#endif
                        pcode->flags |= fIsPtrOp;
                    }
                    addaliasusedef(pcode->alias);
                } else {
                    pcode->flags |= fIsPtrOp;
                }
            }
        }
    }

    addworstcase();
}

#if VERSION < VERSION_GC_2_5
static void precomputeusedefcounts(int flag) {
    PCodeBlock* block;
    PCode* pcode;
    PCodeArg* op;
    int i;
    ObjectUseDef* oud;

    number_of_Uses = number_of_Defs = 0;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (!(pcode->flags & fIsBranch) && pcode->argCount) {
                pcode->useID = number_of_Uses;
                pcode->defID = number_of_Defs;
                op = pcode->args;
                i = pcode->argCount;
                while (i--) {
                    if (op->kind ==
                        PCOp_REGISTER &&
                        op->data.reg.reg >= n_real_registers[op->arg]) {
                        if (op->data.reg.effect & EffectRead) {
                            number_of_Uses++;
                        }
                        if (op->data.reg.effect & EffectWrite) {
                            number_of_Defs++;
                        }
                    }
                    op++;
                }

                if (flag) {
                    if (pcode->flags & fIsRead) {
                        if (pcode->flags & fIsPtrOp) {
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, oud->object)) {
                                    number_of_Uses++;
                                }
                            }
                        } else {
                            number_of_Uses++;
                        }
                    } else if (pcode->flags & fIsWrite) {
                        if (pcode->flags & fIsPtrOp) {
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, oud->object)) {
                                    number_of_Defs++;
                                }
                            }
                        } else {
                            number_of_Defs++;
                        }
                    } else if (pcode->flags & fIsCall) {
                        for (oud = objectusedeflist; oud; oud = oud->next) {
                            if (may_alias_object(pcode, oud->object)) {
                                number_of_Uses++;
                                number_of_Defs++;
                            }
                        }
                    }
                }
            }
        }
    }
}

static void computeusedeflists(int flag) {
    ObjectUseDef* oud;
    RegUseOrDef* list;
    PCodeBlock* block;
    PCode* pcode;
    PCodeArg* op;
    int i;
    UseOrDef* use;
    UseOrDef* def;
    int useID;
    int defID;
    ObjectUseDef* tmp;
    Object* object;

    Uses = oalloc(sizeof(UseOrDef) * number_of_Uses);
    Defs = oalloc(sizeof(UseOrDef) * number_of_Defs);

    reg_Uses[RegClass_GPR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_GPR]);
    reg_Defs[RegClass_GPR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_GPR]);
    for (i = 0; i < used_virtual_registers[RegClass_GPR]; i++) {
        reg_Uses[RegClass_GPR][i] = NULL, reg_Defs[RegClass_GPR][i] = NULL;
    }

    reg_Uses[RegClass_FPR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_FPR]);
    reg_Defs[RegClass_FPR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_FPR]);
    for (i = 0; i < used_virtual_registers[RegClass_FPR]; i++) {
        reg_Uses[RegClass_FPR][i] = NULL, reg_Defs[RegClass_FPR][i] = NULL;
    }

    reg_Uses[RegClass_VR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_VR]);
    reg_Defs[RegClass_VR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_VR]);
    for (i = 0; i < used_virtual_registers[RegClass_VR]; i++) {
        reg_Uses[RegClass_VR][i] = NULL, reg_Defs[RegClass_VR][i] = NULL;
    }

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (!(pcode->flags & fIsBranch) && pcode->argCount) {
                use = &Uses[useID = pcode->useID];
                def = &Defs[defID = pcode->defID];

                op = pcode->args;
                i = pcode->argCount;
                while (i--) {
                    if (op->kind ==
                        PCOp_REGISTER &&
                        op->data.reg.reg >= n_real_registers[op->arg]) {
                        if (op->data.reg.effect & EffectRead) {
                            use->pcode = pcode;
                            use->v.kind = op->kind;
                            use->v.arg = op->arg;
                            use->v.u.reg = op->data.reg.reg;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = useID;
                            list->next = reg_Uses[op->arg][op->data.reg.reg];
                            reg_Uses[op->arg][op->data.reg.reg] = list;
                            use++;
                            useID++;
                        }
                        if (op->data.reg.effect & EffectWrite) {
                            def->pcode = pcode;
                            def->v.kind = op->kind;
                            def->v.arg = op->arg;
                            def->v.u.reg = op->data.reg.reg;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = defID;
                            list->next = reg_Defs[op->arg][op->data.reg.reg];
                            reg_Defs[op->arg][op->data.reg.reg] = list;
                            def++;
                            defID++;
                        }
                    }
                    op++;
                }

                if (flag) {
                    if (pcode->flags & fIsRead) {
                        if (pcode->flags & fIsPtrOp) {
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, object = oud->object)) {
                                    use->pcode = pcode;
                                    use->v.kind = PCOp_MEMORY;
                                    use->v.arg = 1;
                                    use->v.u.object = object;
                                    list = oalloc(sizeof(RegUseOrDef));
                                    list->id = useID;
                                    list->next = oud->uses;
                                    oud->uses = list;
                                    use++;
                                    useID++;
                                }
                            }
                        } else {
                            object = pcode->alias->object;
                            use->pcode = pcode;
                            use->v.kind = PCOp_MEMORY;
                            use->v.arg = 0;
                            use->v.u.object = object;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = useID;
                            tmp = findobjectusedef(object);
                            list->next = tmp->uses;
                            tmp->uses = list;
                        }
                    } else if (pcode->flags & fIsWrite) {
                        if (pcode->flags & fIsPtrOp) {
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, object = oud->object)) {
                                    def->pcode = pcode;
                                    def->v.kind = PCOp_MEMORY;
                                    def->v.arg = 1;
                                    def->v.u.object = object;
                                    list = oalloc(sizeof(RegUseOrDef));
                                    list->id = defID;
                                    list->next = oud->defs;
                                    oud->defs = list;
                                    def++;
                                    defID++;
                                }
                            }
                        } else {
                            object = pcode->alias->object;
                            def->pcode = pcode;
                            def->v.kind = PCOp_MEMORY;
                            def->v.arg = 0;
                            def->v.u.object = object;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = defID;
                            tmp = findobjectusedef(object);
                            list->next = tmp->defs;
                            tmp->defs = list;
                        }
                    } else if (pcode->flags & fIsCall) {
                        for (oud = objectusedeflist; oud; oud = oud->next) {
                            if (may_alias_object(pcode, object = oud->object)) {
                                use->pcode = pcode;
                                use->v.kind = PCOp_MEMORY;
                                use->v.arg = 1;
                                use->v.u.object = object;
                                list = oalloc(sizeof(RegUseOrDef));
                                list->id = useID;
                                list->next = oud->uses;
                                oud->uses = list;
                                use++;
                                useID++;

                                def->pcode = pcode;
                                def->v.kind = PCOp_MEMORY;
                                def->v.arg = 1;
                                def->v.u.object = object;
                                list = oalloc(sizeof(RegUseOrDef));
                                list->id = defID;
                                list->next = oud->defs;
                                oud->defs = list;
                                def++;
                                defID++;
                            }
                        }
                    }
                }
            }
        }
    }
}
#else
static void precomputeusedefcounts(int flag) {
    PCodeBlock* block;
    PCode* pcode;
    PCodeArg* op;
    int i;
    ObjectUseDef* oud;

    number_of_Uses = number_of_Defs = 0;
#if VERSION >= VERSION_GC_2_5
    usedefinfo = oalloc(pcblockcount * sizeof(UseDefInfo));
    memclrw(usedefinfo, pcblockcount * sizeof(UseDefInfo));
#endif

    for (block = pcbasicblocks; block; block = block->nextBlock) {
#if VERSION >= VERSION_GC_2_5
        usedefinfo[block->blockIndex].usestart = number_of_Uses;
        usedefinfo[block->blockIndex].defstart = number_of_Defs;
#endif
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (pcode->argCount) {
                pcode->useID = number_of_Uses;
                pcode->defID = number_of_Defs;
                // 2.6: a for loop (Pro7's while loop takes other registers)
                op = pcode->args;
                for (i = pcode->argCount; i--; op++) {
                    if (op->kind ==
                        PCOp_REGISTER &&
                        op->data.reg.reg >= n_real_registers[op->arg]) {
                        if (op->data.reg.effect & EffectRead) {
                            number_of_Uses++;
                        }
                        if (op->data.reg.effect & EffectWrite) {
                            number_of_Defs++;
                        }
                    }
                }

                if (flag) {
                    if (pcode->flags & fIsRead) {
                        if (pcode->alias->type !=
                            AliasType_ObjectPart &&
                            pcode->alias->type != AliasType_Object) {
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, oud->object)) {
                                    number_of_Uses++;
                                }
                            }
                        } else {
                            number_of_Uses++;
                        }
                    } else if (pcode->flags & fIsWrite) {
                        if (pcode->alias->type !=
                            AliasType_ObjectPart &&
                            pcode->alias->type != AliasType_Object) {
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, oud->object)) {
                                    number_of_Defs++;
                                }
                            }
                        } else {
                            number_of_Defs++;
                        }
                    } else if (pcode->flags & fIsCall) {
                        for (oud = objectusedeflist; oud; oud = oud->next) {
                            if (may_alias_object(pcode, oud->object)) {
                                number_of_Uses++;
                                number_of_Defs++;
                            }
                        }
                    }
                }
            }
        }
#if VERSION >= VERSION_GC_2_5
        usedefinfo[block->blockIndex].usecount =
            number_of_Uses - usedefinfo[block->blockIndex].usestart;
        usedefinfo[block->blockIndex].defcount =
            number_of_Defs - usedefinfo[block->blockIndex].defstart;
#endif
    }
}

static void computeusedeflists(int flag) {
    ObjectUseDef* oud;
    RegUseOrDef* list;
    PCodeBlock* block;
    PCode* pcode;
    PCodeArg* op;
    int i;
    UseOrDef* use;
    UseOrDef* def;
    int useID;
    int defID;
    ObjectUseDef* tmp;
    Object* object;
    RegClass rclass;

    Uses = oalloc(sizeof(UseOrDef) * number_of_Uses);
    Defs = oalloc(sizeof(UseOrDef) * number_of_Defs);

    // 2.6: every register class (Pro7: GPR, FPR and VR, written out)
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        reg_Uses[rclass] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[rclass]);
        reg_Defs[rclass] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[rclass]);
        for (i = 0; i < used_virtual_registers[rclass]; i++) {
            // (one statement: two take other registers)
            reg_Uses[rclass][i] = NULL, reg_Defs[rclass][i] = NULL;
        }
    }

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (pcode->argCount) {
                use = &Uses[useID = pcode->useID];
                def = &Defs[defID = pcode->defID];

                op = pcode->args;
                i = pcode->argCount;
                while (i--) {
                    if (op->kind ==
                        PCOp_REGISTER &&
                        op->data.reg.reg >= n_real_registers[op->arg]) {
                        if (op->data.reg.effect & EffectRead) {
                            use->pcode = pcode;
                            use->v.kind = op->kind;
                            use->v.arg = op->arg;
                            use->v.u.reg = op->data.reg.reg;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = useID;
                            list->next = reg_Uses[op->arg][op->data.reg.reg];
                            reg_Uses[op->arg][op->data.reg.reg] = list;
                            use++;
                            useID++;
                        }
                        if (op->data.reg.effect & EffectWrite) {
                            def->pcode = pcode;
                            def->v.kind = op->kind;
                            def->v.arg = op->arg;
                            def->v.u.reg = op->data.reg.reg;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = defID;
                            list->next = reg_Defs[op->arg][op->data.reg.reg];
                            reg_Defs[op->arg][op->data.reg.reg] = list;
                            def++;
                            defID++;
                        }
                    }
                    op++;
                }

                if (flag) {
                    if (pcode->flags & fIsRead) {
                        if (pcode->alias->type !=
                            AliasType_ObjectPart &&
                            pcode->alias->type != AliasType_Object) {
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, object = oud->object)) {
                                    use->pcode = pcode;
                                    use->v.kind = PCOp_MEMORY;
                                    use->v.arg = 1;
                                    use->v.u.object = object;
                                    list = oalloc(sizeof(RegUseOrDef));
                                    list->id = useID;
                                    list->next = oud->uses;
                                    oud->uses = list;
                                    oud->indirectuses++;
                                    use++;
                                    useID++;
                                }
                            }
                        } else {
                            object = pcode->alias->object;
                            tmp = findobjectusedef(object);
                            use->pcode = pcode;
                            use->v.kind = PCOp_MEMORY;
                            use->v.u.object = object;
                            if (must_alias_object(pcode, object)) {
                                use->v.arg = 0;
                                tmp->directuses++;
                            } else {
                                use->v.arg = 1;
                                tmp->indirectuses++;
                            }
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = useID;
                            list->next = tmp->uses;
                            tmp->uses = list;
                        }
                    } else if (pcode->flags & fIsWrite) {
                        if (pcode->alias->type !=
                            AliasType_ObjectPart &&
                            pcode->alias->type != AliasType_Object) {
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, object = oud->object)) {
                                    def->pcode = pcode;
                                    def->v.kind = PCOp_MEMORY;
                                    def->v.arg = 1;
                                    def->v.u.object = object;
                                    list = oalloc(sizeof(RegUseOrDef));
                                    list->id = defID;
                                    list->next = oud->defs;
                                    oud->defs = list;
                                    oud->indirectdefs++;
                                    def++;
                                    defID++;
                                }
                            }
                        } else {
                            object = pcode->alias->object;
                            def->pcode = pcode;
                            def->v.kind = PCOp_MEMORY;
                            def->v.u.object = object;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = defID;
                            tmp = findobjectusedef(object);
                            if (must_alias_object(pcode, object)) {
                                def->v.arg = 0;
                                tmp->directdefs++;
                            } else {
                                def->v.arg = 1;
                                tmp->indirectdefs++;
                            }
                            list->next = tmp->defs;
                            tmp->defs = list;
                        }
                    } else if (pcode->flags & fIsCall) {
                        for (oud = objectusedeflist; oud; oud = oud->next) {
                            if (may_alias_object(pcode, object = oud->object)) {
                                use->pcode = pcode;
                                use->v.kind = PCOp_MEMORY;
                                use->v.arg = 1;
                                use->v.u.object = object;
                                list = oalloc(sizeof(RegUseOrDef));
                                list->id = useID;
                                list->next = oud->uses;
                                oud->uses = list;
                                oud->indirectuses++;
                                use++;
                                useID++;

                                def->pcode = pcode;
                                def->v.kind = PCOp_MEMORY;
                                def->v.arg = 1;
                                def->v.u.object = object;
                                list = oalloc(sizeof(RegUseOrDef));
                                list->id = defID;
                                list->next = oud->defs;
                                oud->defs = list;
                                oud->indirectdefs++;
                                def++;
                                defID++;
                            }
                        }
                    }
                }
            }
        }
    }
}
#endif

#if VERSION >= VERSION_GC_2_5
static void allocateusedefinfo(void) {
    UseDefInfo* info;
    int i;

    init_compressed_bitvectors(number_of_Defs > number_of_Uses ? number_of_Defs : number_of_Uses);
    for (i = 0, info = usedefinfo; i < pcblockcount; i++, info++) {
        info->defgen = bitvectorcreate_range(info->defstart, info->defcount);
        info->defkill = bitvector_empty_compressed();
        info->defin = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->defout = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->usegen = bitvectorcreate_range(info->usestart, info->usecount);
        info->usekill = bitvector_empty_compressed();
        info->usein = oalloc(4 * ((number_of_Uses + 31) >> 5));
        info->useout = oalloc(4 * ((number_of_Uses + 31) >> 5));
    }
}

static void computelocalusedefinfo(void) {
    PCodeBlock* block;
    PCode* pcode;
    BitVectorRange* usevec10;
    BitVectorRange* defvec0;
    UInt32* defvec4;
    UseOrDef* use;
    UseOrDef* def;
    UInt32* usevec14;
    int useID;
    int defID;
    ObjectUseDef* oud;
    RegUseOrDef* list;
    UseDefInfo* info;

    defvec4 = oalloc(4 * ((number_of_Defs + 31) >> 5));
    usevec14 = oalloc(4 * ((number_of_Uses + 31) >> 5));

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        info = &usedefinfo[block->blockIndex];
        defvec0 = info->defgen;
        usevec10 = info->usegen;
        bitvectorinitialize_range(defvec0, info->defstart, info->defcount, 0);
        bitvectorinitialize(defvec4, number_of_Defs, 0);
        bitvectorinitialize_range(usevec10, info->usestart, info->usecount, 0);
        bitvectorinitialize(usevec14, number_of_Uses, 0);
        bitvectorinitialize(info->defin, number_of_Defs, 0);
        bitvectorinitialize(info->defout, number_of_Defs, 0);
        bitvectorinitialize(info->usein, number_of_Uses, 0);
        bitvectorinitialize(info->useout, number_of_Uses, 0);

        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (pcode->argCount) {
                for (use = &Uses[useID = pcode->useID];
                     useID < number_of_Uses && use->pcode == pcode;
                     use++, useID++) {
                    bitvectorsetbit_range(useID, usevec10);
                    if (use->v.kind == PCOp_REGISTER) {
                        for (list = reg_Defs[use->v.arg][use->v.u.reg]; list; list = list->next) {
                            if (bitvectorgetbit_range(list->id, defvec0)) {
                                bitvectorclearbit_range(useID, usevec10);
                            }
                        }
                    } else {
                        for (list = findobjectusedef(use->v.u.object)->defs; list;
                             list = list->next) {
                            if (must_alias_object(pcode, Defs[list->id].v.u.object)) {
                                if (Defs[list->id].v.arg ==
                                    0 &&
                                    bitvectorgetbit_range(list->id, defvec0)) {
                                    bitvectorclearbit_range(useID, usevec10);
                                }
                            }
                        }
                    }
                }

                for (def = &Defs[defID = pcode->defID];
                     defID < number_of_Defs && def->pcode == pcode;
                     def++, defID++) {
                    if (def->v.kind == PCOp_REGISTER) {
                        for (list = reg_Uses[def->v.arg][def->v.u.reg]; list; list = list->next) {
                            if (Uses[list->id].pcode->block != block) {
                                bitvectorsetbit(list->id, usevec14);
                            }
                        }
                        for (list = reg_Defs[def->v.arg][def->v.u.reg]; list; list = list->next) {
                            if (Defs[list->id].pcode->block != block) {
                                bitvectorsetbit(list->id, defvec4);
                            } else {
                                bitvectorclearbit_range(list->id, defvec0);
                            }
                        }
                    } else if (def->v.arg == 0) {
                        oud = findobjectusedef(def->v.u.object);
                        for (list = oud->uses; list; list = list->next) {
                            if (may_alias_object(pcode, Uses[list->id].v.u.object)) {
                                if (Uses[list->id].pcode->block != block) {
                                    bitvectorsetbit(list->id, usevec14);
                                }
                            }
                        }
                        for (list = oud->defs; list; list = list->next) {
                            if (must_alias_object(pcode, Defs[list->id].v.u.object)) {
                                if (Defs[list->id].pcode->block != block) {
                                    bitvectorsetbit(list->id, defvec4);
                                } else {
                                    bitvectorclearbit_range(list->id, defvec0);
                                }
                            }
                        }
                    }

                    bitvectorsetbit_range(defID, defvec0);
                }
            }
        }

        info->defkill = bitvector_compress(defvec4, number_of_Defs);
        info->usekill = bitvector_compress(usevec14, number_of_Uses);
    }
}

// 2.6: OUT = GEN | (IN & ~KILL), whether OUT changed (TODO: invented name)
static int updateusedefset(
    UInt32* out, UInt32* in, CompressedBitVector* kill, BitVectorRange* gen, int bit_count
) {
    int words;
    int runs;
    int genStart;
    int genLength;
    int changed;
    CompressedBitRun* run;
    int runStart;
    int runLength;
    int i;
    UInt32 bits;
    UInt32 old;

    words = (bit_count + 31) >> 5;
    runs = kill->nruns;
    genStart = gen->firstword;
    genLength = gen->nwords;
    changed = FALSE;
    run = NULL;
    if (runs) {
        run = kill->runs;
        runStart = run->firstword;
        runLength = run->nwords;
    }
    for (i = 0; i < words; i++) {
        old = out[i];
        bits = in[i];
        if (run) {
            if (i >= runStart + runLength) {
                if (--runs != 0) {
                    run = (CompressedBitRun*)&run->words[runLength];
                    runStart = run->firstword;
                    runLength = run->nwords;
                } else {
                    run = NULL;
                }
            }
            if (run && i >= runStart) {
                bits &= ~run->words[i - runStart];
            }
        }
        if (i >= genStart && i < genStart + genLength) {
            bits |= gen->words[i - genStart];
        }
        if (bits != old) {
            changed = TRUE;
            out[i] = bits;
        }
    }
    return changed;
}

static void computeglobalusedefinfo(void) {
    PCLink* preds;
    UseDefInfo* udi;
    PCodeBlock* block;
    UInt32* defVec8;
    int i;
    int flag;

    flag = TRUE;
    while (flag) {
        flag = FALSE;
        for (i = 0; i < pcblockcount; i++) {
            if ((block = depthfirstordering[i])) {
                udi = &usedefinfo[block->blockIndex];
                if ((preds = block->predecessors)) {
                    defVec8 = udi->defin;
                    bitvectorcopy(
                        defVec8, usedefinfo[preds->block->blockIndex].defout, number_of_Defs
                    );
                    for (preds = preds->nextLink; preds; preds = preds->nextLink) {
                        bitvectorunion(
                            defVec8, usedefinfo[preds->block->blockIndex].defout, number_of_Defs
                        );
                    }
                }
                flag |= updateusedefset(
                    udi->defout, udi->defin, udi->defkill, udi->defgen, number_of_Defs
                );
            }
        }
    }
}

static void computeglobaldefuseinfo(void) {
    PCLink* succs;
    UseDefInfo* udi;
    PCodeBlock* block;
    UInt32* useVec1C;
    int i;
    int flag;

    flag = TRUE;
    while (flag) {
        flag = FALSE;
        i = pcblockcount;
        while (i) {
            if ((block = depthfirstordering[--i])) {
                udi = &usedefinfo[block->blockIndex];
                if ((succs = block->successors)) {
                    useVec1C = udi->useout;
                    bitvectorcopy(
                        useVec1C, usedefinfo[succs->block->blockIndex].usein, number_of_Uses
                    );
                    for (succs = succs->nextLink; succs; succs = succs->nextLink) {
                        bitvectorunion(
                            useVec1C, usedefinfo[succs->block->blockIndex].usein, number_of_Uses
                        );
                    }
                }
                flag |= updateusedefset(
                    udi->usein, udi->useout, udi->usekill, udi->usegen, number_of_Uses
                );
            }
        }
    }
}

#else
// 2.0: every set is a full bit vector, and the global passes update them in place
static void allocateusedefinfo(void) {
    UseDefInfo* info;
    int i;

    usedefinfo = oalloc(pcblockcount * sizeof(UseDefInfo));
    for (i = 0, info = usedefinfo; i < pcblockcount; i++, info++) {
        info->defgen = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->defkill = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->defin = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->defout = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->usegen = oalloc(4 * ((number_of_Uses + 31) >> 5));
        info->usekill = oalloc(4 * ((number_of_Uses + 31) >> 5));
        info->usein = oalloc(4 * ((number_of_Uses + 31) >> 5));
        info->useout = oalloc(4 * ((number_of_Uses + 31) >> 5));
    }
}

// 2.0 asks its alias analysis about two PCodes: the use against each definition, the definition
// against each use and definition of its object
static void computelocalusedefinfo(void) {
    PCodeBlock* block;
    PCode* pcode;
    UInt32* defvec0;
    UInt32* defvec4;
    UInt32* usevec10;
    UInt32* usevec14;
    UseOrDef* use;
    UseOrDef* def;
    int useID;
    int defID;
    ObjectUseDef* oud;
    RegUseOrDef* list;
    UseDefInfo* info;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        info = &usedefinfo[block->blockIndex];
        defvec0 = info->defgen;
        defvec4 = info->defkill;
        usevec10 = info->usegen;
        usevec14 = info->usekill;
        bitvectorinitialize(defvec0, number_of_Defs, 0);
        bitvectorinitialize(defvec4, number_of_Defs, 0);
        bitvectorinitialize(usevec10, number_of_Uses, 0);
        bitvectorinitialize(usevec14, number_of_Uses, 0);
        bitvectorinitialize(info->defin, number_of_Defs, 0);
        bitvectorinitialize(info->defout, number_of_Defs, 0);
        bitvectorinitialize(info->usein, number_of_Uses, 0);
        bitvectorinitialize(info->useout, number_of_Uses, 0);

        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (!(pcode->flags & fIsBranch) && pcode->argCount) {
                for (use = &Uses[useID = pcode->useID];
                     useID < number_of_Uses && use->pcode == pcode;
                     use++, useID++) {
                    bitvectorsetbit(useID, usevec10);
                    if (use->v.kind == PCOp_REGISTER) {
                        for (list = reg_Defs[use->v.arg][use->v.u.reg]; list; list = list->next) {
                            if (bitvectorgetbit(list->id, defvec0)) {
                                bitvectorclearbit(useID, usevec10);
                            }
                        }
                    } else {
                        for (list = findobjectusedef(use->v.u.object)->defs; list;
                             list = list->next) {
                            if (uniquely_aliases(pcode, Defs[list->id].pcode)) {
                                if (Defs[list->id].v.arg ==
                                    0 &&
                                    bitvectorgetbit(list->id, defvec0)) {
                                    bitvectorclearbit(useID, usevec10);
                                }
                            }
                        }
                    }
                }

                for (def = &Defs[defID = pcode->defID];
                     defID < number_of_Defs && def->pcode == pcode;
                     def++, defID++) {
                    if (def->v.kind == PCOp_REGISTER) {
                        for (list = reg_Uses[def->v.arg][def->v.u.reg]; list; list = list->next) {
                            if (Uses[list->id].pcode->block != block) {
                                bitvectorsetbit(list->id, usevec14);
                            }
                        }
                        for (list = reg_Defs[def->v.arg][def->v.u.reg]; list; list = list->next) {
                            if (Defs[list->id].pcode->block != block) {
                                bitvectorsetbit(list->id, defvec4);
                            } else {
                                bitvectorclearbit(list->id, defvec0);
                            }
                        }
                    } else if (def->v.arg == 0) {
                        oud = findobjectusedef(def->v.u.object);
                        for (list = oud->uses; list; list = list->next) {
                            if (may_alias(pcode, Uses[list->id].pcode)) {
                                if (Uses[list->id].pcode->block != block) {
                                    bitvectorsetbit(list->id, usevec14);
                                }
                            }
                        }
                        for (list = oud->defs; list; list = list->next) {
                            if (uniquely_aliases(pcode, Defs[list->id].pcode)) {
                                if (Defs[list->id].pcode->block != block) {
                                    bitvectorsetbit(list->id, defvec4);
                                } else {
                                    bitvectorclearbit(list->id, defvec0);
                                }
                            }
                        }
                    }

                    bitvectorsetbit(defID, defvec0);
                }
            }
        }
    }
}

static void computeglobalusedefinfo(void) {
    UInt32* defVec0;
    PCLink* preds;
    UseDefInfo* udi;
    PCodeBlock* block;
    UInt32* defVec8;
    int i;
    int flag;
    int bitvecsize;
    UInt32* defVec4;
    UInt32 val;
    UInt32* defVecC;
    int j;

    bitvecsize = (number_of_Defs + 31) >> 5;
    flag = TRUE;
    while (flag) {
        flag = FALSE;
        for (i = 0; i < pcblockcount; i++) {
            if ((block = depthfirstordering[i])) {
                udi = &usedefinfo[block->blockIndex];
                if ((preds = block->predecessors)) {
                    defVec8 = udi->defin;
                    bitvectorcopy(
                        defVec8, usedefinfo[preds->block->blockIndex].defout, number_of_Defs
                    );
                    for (preds = preds->nextLink; preds; preds = preds->nextLink) {
                        bitvectorunion(
                            defVec8, usedefinfo[preds->block->blockIndex].defout, number_of_Defs
                        );
                    }
                }

                defVecC = udi->defout;
                defVec8 = udi->defin;
                defVec0 = udi->defgen;
                defVec4 = udi->defkill;
                for (j = 0; j < bitvecsize; j++) {
                    val = *defVec0 | (*defVec8 & ~*defVec4);
                    if (val != *defVecC) {
                        *defVecC = val;
                        flag = TRUE;
                    }
                    defVec8++;
                    defVecC++;
                    defVec4++;
                    defVec0++;
                }
            }
        }
    }
}

static void computeglobaldefuseinfo(void) {
    PCLink* succs;
    UseDefInfo* udi;
    PCodeBlock* block;
    UInt32* useVec1C;
    int i;
    int flag;
    int bitvecsize;
    UInt32* useVec10;
    UInt32* useVec14;
    UInt32* useVec18;
    UInt32 val;
    int j;

    bitvecsize = (number_of_Uses + 31) >> 5;
    flag = TRUE;
    while (flag) {
        flag = FALSE;
        i = pcblockcount;
        while (i) {
            if ((block = depthfirstordering[--i])) {
                udi = &usedefinfo[block->blockIndex];
                if ((succs = block->successors)) {
                    useVec1C = udi->useout;
                    bitvectorcopy(
                        useVec1C, usedefinfo[succs->block->blockIndex].usein, number_of_Uses
                    );
                    for (succs = succs->nextLink; succs; succs = succs->nextLink) {
                        bitvectorunion(
                            useVec1C, usedefinfo[succs->block->blockIndex].usein, number_of_Uses
                        );
                    }
                }

                useVec1C = udi->useout;
                useVec18 = udi->usein;
                useVec10 = udi->usegen;
                useVec14 = udi->usekill;
                for (j = 0; j < bitvecsize; j++) {
                    val = *useVec10 | (*useVec1C & ~*useVec14);
                    if (val != *useVec18) {
                        *useVec18 = val;
                        flag = TRUE;
                    }
                    useVec18++;
                    useVec1C++;
                    useVec14++;
                    useVec10++;
                }
            }
        }
    }
}
#endif

void computeusedefchains(int flag) {
#if VERSION >= VERSION_GC_2_5
    RegClass rclass;
#endif

    if (flag) {
        gather_alias_info();
        precomputememoryusedefs();
    }

    precomputeusedefcounts(flag);
    computeusedeflists(flag);
    allocateusedefinfo();
    computelocalusedefinfo();
    computedepthfirstordering();
    computeglobalusedefinfo();
    computeglobaldefuseinfo();

#if VERSION >= VERSION_GC_2_5
    // 2.5
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        last_usedef_register[rclass] = used_virtual_registers[rclass];
    }
#endif
}

#if VERSION >= VERSION_GC_2_5
// 2.5 (2.0's is in CopyPropagation.c)
int precedes(PCode* a, PCode* b) {
    // walks with the parameter (Pro7: a local `scan`, which takes another register)
    for (a = a->nextPCode; a; a = a->nextPCode) {
        if (a == b) {
            return TRUE;
        }
    }

    return FALSE;
}
#endif

// 2.6 (Wii's name and spelling): whether no definition of register REG of class RCLASS lies on the
// path from the definition CANDIDATE (number DEFID) to USE, given the definitions DEFVEC reaching
// USE's block
int canidate_reaches_use(
    PCode* candidate, int defID, int reg, PCode* use, UInt32* defvec, RegClass rclass
) {
    PCodeBlock* block;
    PCode* pcode;
    PCodeArg* op;
    int i;

    block = use->block;
    if (candidate->block == block && precedes(candidate, use)) {
        for (pcode = candidate->nextPCode; pcode && pcode != use; pcode = pcode->nextPCode) {
            op = pcode->args;
            i = pcode->argCount;
            while (i--) {
                if (op->kind ==
                    PCOp_REGISTER &&
                    op->arg ==
                    rclass &&
                    (op->data.reg.effect & EffectWrite) &&
                    op->data.reg.reg == reg) {
                    return FALSE;
                }
                op++;
            }
        }
    } else {
        if (!bitvectorgetbit(defID, defvec)) {
            return FALSE;
        }
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (pcode == use) {
                break;
            }
            op = pcode->args;
            i = pcode->argCount;
            while (i--) {
                if (op->kind ==
                    PCOp_REGISTER &&
                    op->arg ==
                    rclass &&
                    (op->data.reg.effect & EffectWrite) &&
                    op->data.reg.reg == reg) {
                    return FALSE;
                }
                op++;
            }
        }
    }

    return TRUE;
}
