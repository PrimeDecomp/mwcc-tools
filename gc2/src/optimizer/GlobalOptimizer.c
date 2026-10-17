#include "compiler/GlobalOptimizer.h"
#include "compiler/AddPropagation.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/CodeMotion.h"
#include "compiler/ConstantPropagation.h"
#include "compiler/CopyPropagation.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/LoadDeletion.h"
#include "compiler/LoopDetection.h"
#include "compiler/LoopOptimization.h"
#include "compiler/PCodeListing.h"
#include "compiler/Peephole.h"
#include "compiler/StrengthReduction.h"
#include "compiler/UseDefChains.h"
#include "compiler/ValueNumbering.h"
#include "compiler/VectorArraysToRegs.h"

static void optimizelevel2(Object* func) {
    removecommonsubexpressions(func, 1);
    if (removedcommonsubexpressions && copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CSE");
    }

    propagatecopyinstructions(func, 1);
    propagateaddinstructions(func);
}

static void optimizelevel3(Object* func) {
    if (copts.peephole) {
        if (copts.schedule_factor == 0 && copts.optimizationlevel > 1) {
            peepholemergeblocks(func, 0);
        }

        peepholeoptimizeforward(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE FORWARD");
        }
    }

    removecommonsubexpressions(func, 0);
    if (removedcommonsubexpressions && copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING");
    }

    propagatecopyinstructions(func, 0);
    propagatedcopies = 0;
    propagateaddinstructions(func);

    findloopsinflowgraph();
    if (loopsinflowgraph) {
        computeusedefchains(1);
        analyzeloopsinflowgraph();
        moveloopinvariantcode();
        if (movedloopinvariantcode && copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CODE MOTION");
        }

        computeusedefchains(0);
        analyzeForCountableLoops(loopsinflowgraph);
        strengthreduceloops();
        if (strengthreducedloops) {
            if (copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER STRENGTH REDUCTION");
            }
            propagatecopyinstructions(func, 1);
        }

        optimizeloops();
        if (optimizedloops) {
            if (copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER LOOP TRANSFORMATIONS");
            }
            propagatecopyinstructions(func, 1);
            propagateaddinstructions(func);
        }
    }

    if (!propagatedcopies) {
        propagatecopyinstructions(func, 1);
        if (propagatedcopies && copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER COPY PROPAGATION");
        }
    }

    propagateconstants();
    if (propagatedconstants) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CONSTANT PROPAGATION");
        }

        deletedeadloads(func);
        propagateaddinstructions(func);
    }

    if (copts.peephole) {
        if (copts.schedule_factor == 0 && copts.optimizationlevel > 1) {
            peepholemergeblocks(func, 0);
        }

        peepholeoptimizeforward(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE FORWARD 2");
        }
    }

    removecommonsubexpressions(func, 1);
    if (removedcommonsubexpressions) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING 2");
        }
        propagatecopyinstructions(func, 1);
    }
}

static void optimizelevel4(Object* func) {
    if (copts.peephole) {
        if (copts.schedule_factor == 0 && copts.optimizationlevel > 1) {
            peepholemergeblocks(func, 0);
        }

        peepholeoptimizeforward(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE FORWARD");
        }
    }

    removecommonsubexpressions(func, 0);
    if (removedcommonsubexpressions && copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING");
    }

    propagatecopyinstructions(func, 0);
    propagatedcopies = 0;
    propagateaddinstructions(func);

    findloopsinflowgraph();
    if (loopsinflowgraph) {
        computeusedefchains(1);
        analyzeloopsinflowgraph();
        moveloopinvariantcode();
        if (movedloopinvariantcode && copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CODE MOTION");
        }

        computeusedefchains(0);
        analyzeForCountableLoops(loopsinflowgraph);
        strengthreduceloops();
        if (strengthreducedloops) {
            if (copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER STRENGTH REDUCTION");
            }
            propagatecopyinstructions(func, 1);
            propagatedcopies = 0;
        }

        optimizeloops();
        if (optimizedloops) {
            if (copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER LOOP TRANSFORMATIONS");
            }
            propagatecopyinstructions(func, 1);
            propagateaddinstructions(func);
        }
    }

    if (!propagatedcopies) {
        propagatecopyinstructions(func, 1);
    }

    propagateconstants();
    if (propagatedconstants) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CONSTANT PROPAGATION");
        }

        deletedeadloads(func);
        if (propagatedconstants) {
            propagatecopyinstructions(func, 1);
        }
        propagateaddinstructions(func);

        if (optimizedloop_full_unroll) {
            changearraytoregisters();
            if (optimizedloop_trans_regs && copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER ARRAY => REGISTER TRANSFORM");
                propagateconstants();
                if (copts.debuglisting) {
                    pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CONSTANT PROPAGATION 2");
                }
                if (propagatedconstants) {
                    propagatecopyinstructions(func, 1);
                }
            }
        }
    }

    if (copts.peephole) {
        if (copts.schedule_factor == 0 && copts.optimizationlevel > 1) {
            peepholemergeblocks(func, 0);
        }

        peepholeoptimizeforward(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE FORWARD 2");
        }
    }

    removecommonsubexpressions(func, 1);
    if (removedcommonsubexpressions) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING 2");
        }
        propagatecopyinstructions(func, 1);
    }

    if (has_altivec_arrays && vectorarraystoregs()) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VECTOR ARRAY CONVERSION");
        }
        propagatecopyinstructions(func, 0);
        propagatecopyinstructions(func, 1);
    }

    findloopsinflowgraph();
    if (!loopsinflowgraph) {
        return;
    }

    computeusedefchains(1);
    analyzeloopsinflowgraph();
    moveloopinvariantcode();
    if (movedloopinvariantcode && copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER CODE MOTION 2");
    }

    removecommonsubexpressions(func, 1);
    if (removedcommonsubexpressions) {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING 3");
        }
        propagatecopyinstructions(func, 1);
    }
}

void globallyoptimizepcode(Object* func) {
    if (copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "BEFORE GLOBAL OPTIMIZATION");
    }

    if (copts.optimizationlevel == 2 || (has_catch_blocks && copts.optimizationlevel > 2)) {
        optimizelevel2(func);
    } else if (copts.optimizationlevel == 3) {
        optimizelevel3(func);
    } else if (copts.optimizationlevel == 4) {
        optimizelevel4(func);
    }
}
