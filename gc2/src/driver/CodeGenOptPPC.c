#include "driver/CodeGenOptPPC.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/InstrSelection.h"
#include "driver/CCompiler.h"
#include "driver/CWSecretPluginCallbacks.h"
#include "driver/pref_structs.h"

static short pref_versions;

void CodeGen_InitCompiler(void) {
    init_cgdispatch();
}

void CodeGen_TermCompiler(void) {}

void CodeGen_InitBackEndOptions(void) {
    PBackEnd pb;
    PProject pp;
    PLinker pl;
    Handle handle;

    CWSecretGetNamedPreferences(cparams.context, "PPC EABI CodeGen", &handle);
    pb = *((PBackEnd*)*handle);
    pref_versions = pb.version;

    CWSecretGetNamedPreferences(cparams.context, "PPC EABI Project", &handle);
    pp = *((PProject*)*handle);

    CWSecretGetNamedPreferences(cparams.context, "PPC EABI Linker", &handle);
    pl = *((PLinker*)*handle);

    cparamblkptr = &cparams;
    // (misaligned_mem_access and unsafe_global_reg_vars are set again below: the first stores are
    // dropped, but removing either changes the code)
    copts.function_align = 16;
    copts.misaligned_mem_access = 1;
    copts.switch_tables = 1;
    copts.prepare_compress = 0;
    copts.min_struct_alignment = 4;
#if VERSION >= VERSION_GC_2_5
    copts.old_style_bitfields = 0;
#endif
    copts.unsafe_global_reg_vars = 0;
    copts.ignore_global_reg_vars = 0;
    copts.altivec_model = 0;
    copts.altivec_vrsave = 1;
    copts.cats = 1;
    copts.littleendian = !pp.endian;
    copts.readonly_strings = pb.readonlystrings;
    copts.misaligned_mem_access = 0;
    copts.unsafe_global_reg_vars = 1;
    copts.schedule_factor = 2;

    // Each older panel version numbers the processors its own way. The cases fall through, but each
    // switch reads pb.processor again, so the oldest mapping that knows the value wins (the
    // original's bug: a version 6 panel's 555, 4, comes out as a 601).
    switch (pb.version) {
        case 6:
        case 7:
        case 8:
        case 9:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC555;
                    break;
                case 5:
                    copts.processor = CPU_PPC601;
                    break;
                case 6:
                    copts.processor = CPU_PPC602;
                    break;
                case 7:
                    copts.processor = CPU_PPC603;
                    break;
                case 8:
                    copts.processor = CPU_PPC603e;
                    break;
                case 9:
                    copts.processor = CPU_PPC604;
                    break;
                case 10:
                    copts.processor = CPU_PPC604e;
                    break;
                case 11:
                    copts.processor = CPU_PPC740;
                    break;
                case 12:
                    copts.processor = CPU_PPC750;
                    break;
                case 13:
                    copts.processor = CPU_PPC801;
                    break;
                case 14:
                    copts.processor = CPU_PPC821;
                    break;
                case 15:
                    copts.processor = CPU_PPC823;
                    break;
                case 16:
                    copts.processor = CPU_PPC850;
                    break;
                case 17:
                    copts.processor = CPU_PPC860;
                    break;
                case 18:
                    copts.processor = CPU_PPC8260;
                    break;
                case 19:
                default:
                    copts.processor = CPU_Generic;
                    break;
            }
            // fall through
        case 5:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC601;
                    break;
                case 5:
                    copts.processor = CPU_PPC602;
                    break;
                case 6:
                    copts.processor = CPU_PPC603;
                    break;
                case 7:
                    copts.processor = CPU_PPC603e;
                    break;
                case 8:
                    copts.processor = CPU_PPC604;
                    break;
                case 9:
                    copts.processor = CPU_PPC604e;
                    break;
                case 10:
                    copts.processor = CPU_PPC740;
                    break;
                case 11:
                    copts.processor = CPU_PPC750;
                    break;
                case 12:
                    copts.processor = CPU_PPC801;
                    break;
                case 13:
                    copts.processor = CPU_PPC821;
                    break;
                case 14:
                    copts.processor = CPU_PPC823;
                    break;
                case 15:
                    copts.processor = CPU_PPC850;
                    break;
                case 16:
                    copts.processor = CPU_PPC860;
                    break;
                case 17:
                default:
                    copts.processor = CPU_Generic;
                    break;
            }
            // fall through
        case 4:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC601;
                    break;
                case 5:
                    copts.processor = CPU_PPC602;
                    break;
                case 6:
                    copts.processor = CPU_PPC603;
                    break;
                case 7:
                    copts.processor = CPU_PPC603e;
                    break;
                case 8:
                    copts.processor = CPU_PPC604;
                    break;
                case 9:
                    copts.processor = CPU_PPC604e;
                    break;
                case 10:
                    copts.processor = CPU_PPC740;
                    break;
                case 11:
                    copts.processor = CPU_PPC750;
                    break;
                case 12:
                    copts.processor = CPU_PPC801;
                    break;
                case 13:
                    copts.processor = CPU_PPC821;
                    break;
                case 14:
                    copts.processor = CPU_PPC823;
                    break;
                case 15:
                    copts.processor = CPU_PPC860;
                    break;
                case 16:
                default:
                    copts.processor = CPU_PPC850;
                    break;
            }
            // fall through
        case 3:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC601;
                    break;
                case 5:
                    copts.processor = CPU_PPC602;
                    break;
                case 6:
                    copts.processor = CPU_PPC603;
                    break;
                case 7:
                    copts.processor = CPU_PPC604;
                    break;
                case 8:
                    copts.processor = CPU_PPC740;
                    break;
                case 9:
                    copts.processor = CPU_PPC750;
                    break;
                case 10:
                    copts.processor = CPU_PPC801;
                    break;
                case 11:
                    copts.processor = CPU_PPC821;
                    break;
                case 12:
                    copts.processor = CPU_PPC823;
                    break;
                case 13:
                    copts.processor = CPU_PPC860;
                    break;
                case 14:
                default:
                    copts.processor = CPU_Generic;
                    break;
            }
            // fall through
        case 1:
        case 2:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC601;
                    break;
                case 5:
                    copts.processor = CPU_PPC602;
                    break;
                case 6:
                    copts.processor = CPU_PPC603;
                    break;
                case 7:
                    copts.processor = CPU_PPC604;
                    break;
                case 8:
                    copts.processor = CPU_PPC740;
                    break;
                case 9:
                    copts.processor = CPU_PPC750;
                    break;
                case 10:
                    copts.processor = CPU_PPC801;
                    break;
                case 11:
                    copts.processor = CPU_PPC821;
                    break;
                case 12:
                    copts.processor = CPU_PPC860;
                    break;
                case 13:
                    copts.processor = CPU_Generic;
                    break;
            }
            break;
        default:
            copts.processor = pb.processor;
    }

    switch (copts.processor) {
        case CPU_PPC601:
            copts.scheduling = 1;
            break;
        case CPU_PPC603:
            copts.scheduling = 2;
            break;
        case CPU_PPC603e:
        case CPU_PPC8240:
        case CPU_PPC8260:
            copts.scheduling = 5;
            break;
        case CPU_PPC604:
            copts.scheduling = 3;
            break;
        case CPU_PPC604e:
            copts.scheduling = 6;
            break;
        case CPU_PPC740:
        case CPU_PPC750:
        case CPU_PPCGEKKO:
            copts.scheduling = 4;
            break;
        case CPU_PPC7400:
            copts.scheduling = 7;
            break;
        case CPU_PPC7450:
            copts.scheduling = 10;
            break;
        // (an explicit case, as default's: without it the code differs)
        case CPU_PPC602:
            copts.scheduling = 8;
            break;
        default:
            copts.scheduling = 8;
            break;
    }

    if (pb.schedule) {
        copts.schedule_factor = 2;
    } else {
        copts.schedule_factor = 0;
    }

    copts.gen_isel = 0;
    copts.gen_fsel = 0;
    copts.peephole = pb.peephole;
    copts.structalignment = pb.structalignment;
    copts.function_align = 4 << pb.funcalign;
    copts.pool_data = pb.pooldata;
    copts.sdata2_threshold = pp.sdata2threshold;
    copts.sdata_threshold = pp.sdatathreshold;
    copts.no_comment_records = pp.no_comment_records;
    copts.dwarf_relocations = 1;
    copts.dwarf_siblings = 1;
    copts.common = pb.common;
    copts.absolutepath = pl.symfullpath;
    copts.use_lmw_stmw = pb.use_lmw_stmw;
    copts.rel109_offset = 0;
    copts.use_fp = 0;
    copts.soft_float = 0;
    copts.fp_contract = 0;
    if (pb.floatingpoint) {
        copts.use_fp = 1;
        if (pb.floatingpoint == 1) {
            copts.soft_float = 1;
        } else {
            copts.fp_contract = pb.fp_contract;
        }
    }
    if (copts.processor == CPU_PPCGEKKO) {
        copts.ibm_stackframe = 1;
        copts.gecko_float_typecons = 0;
        copts.disable_vector_frame = 0;
    } else {
        copts.ibm_stackframe = 0;
    }
    copts.altivec_model = pb.altivec;
    if (pb.vrsave) {
        copts.altivec_vrsave = 1;
    } else {
        copts.altivec_vrsave = 0;
    }
    copts.debuglisting = 0;
    copts.unroll_speculative = 1;
    copts.unroll_instr_limit = 70;
    copts.unroll_factor_limit = 10;
    copts.opt_bcc_lr_ctr = 1;
    copts.optimizewithasm = 1;
    copts.strengthreductionstrict = 1;
    copts.bc_range = 0x10000;
    copts.gprfloatcopy = 0;
}

void Test_Version_Numbers(void) {}
