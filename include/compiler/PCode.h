#ifndef COMPILER_PCODE_H
#define COMPILER_PCODE_H

#include <stddef.h>
#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
struct BlockOrderEntry {
    struct PCodeBlock *block;
    struct PCodeBlockLink *cursor;
};
#pragma pack(pop)
#pragma pack(push, 2)
union PCodeOperandValue {
    struct PCodeLabel *label;
#if VERSION >= VERSION_GC_1_3
    /* (1.3: the value sits at +2 of the operand; a register's number at +4, after its effect flags) */
    struct {
        unsigned short reg_effect;
        short reg;
    };
#else
    short reg;
#endif
    int signed_value;
    signed long immediate_value;
    unsigned int unsigned_value;
};
#pragma pack(pop)
/* What a PCode operand is (PCodeOperand.kind): a register of a class (the call clobber list builds r0, r3-r12 as kind 0,
 * f0-f13 as 1, v0-v19 as 9, cr0/1/6/7 as 3; addic. writes XER as 2 and cr0 as 3), an immediate (MTSPR lists it as a
 * number), a symbol (the listing prints its object), a label (the listing prints its block). Numbers: kind is a byte. */
#define PCOp_GPR 0
#define PCOp_FPR 1
#define PCOp_SPR 2
#define PCOp_CRFIELD 3
#define PCOp_IMMEDIATE BY_VERSION(4, 2)
#define PCOp_MEMORY BY_VERSION(5, 3)
#define PCOp_LABEL BY_VERSION(6, 4)
#define PCOp_VR 9
/* A GPR operand: 1.3 keeps every register under kind 0 and names the class in arg (4 = GPR). */
#define PCOP_IS_GPR(op) BY_VERSION((op)->kind == PCOp_GPR, (op)->kind == PCOp_GPR && (op)->arg == 4)
#define PCOP_IS_FPR(op) BY_VERSION((op)->kind == PCOp_FPR, (op)->kind == PCOp_GPR && (op)->arg == 3)
#define PCOP_IS_VR(op) BY_VERSION((op)->kind == PCOp_VR, (op)->kind == PCOp_GPR && (op)->arg == 2)
#define PCOP_IS_CRFIELD(op) BY_VERSION((op)->kind == PCOp_CRFIELD, (op)->kind == PCOp_GPR && (op)->arg == 1)
#define PCOP_IS_SPR(op) BY_VERSION((op)->kind == PCOp_SPR, (op)->kind == PCOp_GPR && (op)->arg == 0)

#pragma pack(push, 2)
#if VERSION >= VERSION_GC_1_3
/* 1.3 register operand view (kind 0): the other kinds keep their value at +2 (see PCodeOperandAlt). */
struct PCodeOperand {
    unsigned char kind;
    char arg; /* 0x01: register class for kind 0 (GPR/FPR/...) */
    union {
        unsigned short flags;
        struct {
            PCodeOperandValue value;
            struct Object *object;
        };
        struct {
            int imm32;
            struct Object *altobject;
        };
        unsigned char placeholder[10]; /* (keeps the operand 12 bytes) */
    };
};
/* 1.3 view of the operands that are not registers (labels, immediates): value at +2, object at +6. */
#pragma pack(push, 2)
struct PCodeOperandAlt {
    unsigned char kind;
    unsigned char arg;
    union {
        struct PCodeLabel *label;
        short imm16;
        int imm32;
    } u;
    struct Object *object;
    unsigned char unknown_0a[2];
};
#pragma pack(pop)
#define PCODE_OPERAND_ALT(instr, i) ((struct PCodeOperandAlt *)&(instr)->operandData.operands[i])
#else
struct PCodeOperand {
    unsigned char kind;
    signed char flags;
    PCodeOperandValue value;
    struct Object *object;
    unsigned char unknown_0a[2];
};
#endif
/* Operand kind 7: label difference, as format_operand tests before reading it. */
struct PCodeLabelDifference {
    UInt8 kind;
    UInt8 negate;
    SInt16 addend;
    struct PCodeLabel *subtractLabel;
    struct PCodeLabel *addLabel;
};
#pragma pack(pop)
/* The PCode opcodes, as the opcode table (gPCodeOpcodeDescriptors) names them; defined as numbers: a short compared with an
 * enumerator compiles differently from one compared with a literal. */
#define PC_B 0
#define PC_BL 1
#define PC_BC 2
#define PC_BCLR 3
#define PC_BCCTR 4
#define PC_BT 5
#define PC_BTLR 6
#define PC_BTCTR 7
#define PC_BF 8
#define PC_BFLR 9
#define PC_BFCTR 10
#define PC_BDNZ 11
#define PC_BDNZT 12
#define PC_BDNZF 13
#define PC_BDZ 14
#define PC_BDZT 15
#define PC_BDZF 16
#define PC_BLR 17
#define PC_BCTR 18
#define PC_BCTRL 19
#define PC_BLRL 20
#define PC_LBZ 21
#define PC_LBZU 22
#define PC_LBZX 23
#define PC_LBZUX 24
#define PC_LHZ 25
#define PC_LHZU 26
#define PC_LHZX 27
#define PC_LHZUX 28
#define PC_LHA 29
#define PC_LHAU 30
#define PC_LHAX 31
#define PC_LHAUX 32
#define PC_LHBRX 33
#define PC_LWZ 34
#define PC_LWZU 35
#define PC_LWZX 36
#define PC_LWZUX 37
#define PC_LWBRX 38
#define PC_LMW 39
#define PC_STB 40
#define PC_STBU 41
#define PC_STBX 42
#define PC_STBUX 43
#define PC_STH 44
#define PC_STHU 45
#define PC_STHX 46
#define PC_STHUX 47
#define PC_STHBRX 48
#define PC_STW 49
#define PC_STWU 50
#define PC_STWX 51
#define PC_STWUX 52
#define PC_STWBRX 53
#define PC_STMW 54
#define PC_DCBF 55
#define PC_DCBST 56
#define PC_DCBT 57
#define PC_DCBTST 58
#define PC_DCBZ 59
#define PC_ADD 60
#define PC_ADDC 61
#define PC_ADDE 62
#define PC_ADDI 63
#define PC_ADDIC 64
#define PC_ADDICR 65
#define PC_ADDIS 66
#define PC_ADDME 67
#define PC_ADDZE 68
#define PC_DIVW 69
#define PC_DIVWU 70
#define PC_MULHW 71
#define PC_MULHWU 72
#define PC_MULLI 73
#define PC_MULLW 74
#define PC_NEG 75
#define PC_SUBF 76
#define PC_SUBFC 77
#define PC_SUBFE 78
#define PC_SUBFIC 79
#define PC_SUBFME 80
#define PC_SUBFZE 81
#define PC_CMPI 82
#define PC_CMP 83
#define PC_CMPLI 84
#define PC_CMPL 85
#define PC_ANDI 86
#define PC_ANDIS 87
#define PC_ORI 88
#define PC_ORIS 89
#define PC_XORI 90
#define PC_XORIS 91
#define PC_AND 92
#define PC_OR 93
#define PC_XOR 94
#define PC_NAND 95
#define PC_NOR 96
#define PC_EQV 97
#define PC_ANDC 98
#define PC_ORC 99
#define PC_EXTSB 100
#define PC_EXTSH 101
#define PC_CNTLZW 102
#define PC_RLWINM 103
#define PC_RLWNM 104
#define PC_RLWIMI 105
#define PC_SLW 106
#define PC_SRW 107
#define PC_SRAWI 108
#define PC_SRAW 109
#define PC_CRAND 110
#define PC_CRANDC 111
#define PC_CREQV 112
#define PC_CRNAND 113
#define PC_CRNOR 114
#define PC_CROR 115
#define PC_CRORC 116
#define PC_CRXOR 117
#define PC_MCRF 118
#define PC_MTXER 119
#define PC_MTCTR 120
#define PC_MTLR 121
#define PC_MTCRF 122
#define PC_MTMSR 123
#define PC_MTSPR 124
#define PC_MFMSR 125
#define PC_MFSPR 126
#define PC_MFXER 127
#define PC_MFCTR 128
#define PC_MFLR 129
#define PC_MFCR 130
#define PC_MFFS 131
#define PC_MTFSF 132
#define PC_EIEIO 133
#define PC_ISYNC 134
#define PC_SYNC 135
#define PC_RFI 136
#define PC_LI 137
#define PC_LIS 138
#define PC_MR 139
#define PC_NOP 140
#define PC_NOT 141
#define PC_LFS 142
#define PC_LFSU 143
#define PC_LFSX 144
#define PC_LFSUX 145
#define PC_LFD 146
#define PC_LFDU 147
#define PC_LFDX 148
#define PC_LFDUX 149
#define PC_STFS 150
#define PC_STFSU 151
#define PC_STFSX 152
#define PC_STFSUX 153
#define PC_STFD 154
#define PC_STFDU 155
#define PC_STFDX 156
#define PC_STFDUX 157
#define PC_FMR 158
#define PC_FABS 159
#define PC_FNEG 160
#define PC_FNABS 161
#define PC_FADD 162
#define PC_FADDS 163
#define PC_FSUB 164
#define PC_FSUBS 165
#define PC_FMUL 166
#define PC_FMULS 167
#define PC_FDIV 168
#define PC_FDIVS 169
#define PC_FMADD 170
#define PC_FMADDS 171
#define PC_FMSUB 172
#define PC_FMSUBS 173
#define PC_FNMADD 174
#define PC_FNMADDS 175
#define PC_FNMSUB 176
#define PC_FNMSUBS 177
#define PC_FRES 178
#define PC_FRSQRTE 179
#define PC_FSEL 180
#define PC_FRSP 181
#define PC_FCTIW 182
#define PC_FCTIWZ 183
#define PC_FCMPU 184
#define PC_FCMPO 185
#define PC_LWARX 186
#define PC_LSWI 187
#define PC_LSWX 188
#define PC_STFIWX 189
#define PC_STSWI 190
#define PC_STSWX 191
#define PC_STWCX 192
#define PC_ECIWX 193
#define PC_ECOWX 194
#define PC_DCBI 195
#define PC_ICBI 196
#define PC_MCRFS 197
#define PC_MCRXR 198
#define PC_MFTB 199
#define PC_MFSR 200
#define PC_MTSR 201
#define PC_MFSRIN 202
#define PC_MTSRIN 203
#define PC_MTFSB0 204
#define PC_MTFSB1 205
#define PC_MTFSFI 206
#define PC_SC 207
#define PC_FSQRT 208
#define PC_FSQRTS 209
#define PC_TLBIA 210
#define PC_TLBIE 211
#define PC_TLBLD 212
#define PC_TLBLI 213
#define PC_TLBSYNC 214
#define PC_TW 215
#define PC_TRAP 216
#define PC_TWI 217
#define PC_OPWORD 218
#define PC_MFROM 219
#define PC_DSA 220
#define PC_ESA 221
#define PC_DCCCI 222
#define PC_DCREAD 223
#define PC_ICBT 224
#define PC_ICCCI 225
#define PC_ICREAD 226
#define PC_RFCI 227
#define PC_TLBRE 228
#define PC_TLBSX 229
#define PC_TLBWE 230
#define PC_WRTEE 231
#define PC_WRTEEI 232
#define PC_MFDCR 233
#define PC_MTDCR 234
#define PC_DCBA 235
#define PC_DSS BY_VERSION(236, 239)
#define PC_DSSALL BY_VERSION(237, 240)
#define PC_DST BY_VERSION(238, 241)
#define PC_DSTT BY_VERSION(239, 242)
#define PC_DSTST BY_VERSION(240, 243)
#define PC_DSTSTT BY_VERSION(241, 244)
#define PC_LVEBX BY_VERSION(242, 245)
#define PC_LVEHX BY_VERSION(243, 246)
#define PC_LVEWX BY_VERSION(244, 247)
#define PC_LVSL BY_VERSION(245, 248)
#define PC_LVSR BY_VERSION(246, 249)
#define PC_LVX BY_VERSION(247, 250)
#define PC_LVXL BY_VERSION(248, 251)
#define PC_STVEBX BY_VERSION(249, 252)
#define PC_STVEHX BY_VERSION(250, 253)
#define PC_STVEWX BY_VERSION(251, 254)
#define PC_STVX BY_VERSION(252, 255)
#define PC_STVXL BY_VERSION(253, 256)
#define PC_MFVSCR BY_VERSION(254, 257)
#define PC_MTVSCR BY_VERSION(255, 258)
#define PC_VADDCUW BY_VERSION(256, 259)
#define PC_VADDFP BY_VERSION(257, 260)
#define PC_VADDSBS BY_VERSION(258, 261)
#define PC_VADDSHS BY_VERSION(259, 262)
#define PC_VADDSWS BY_VERSION(260, 263)
#define PC_VADDUBM BY_VERSION(261, 264)
#define PC_VADDUBS BY_VERSION(262, 265)
#define PC_VADDUHM BY_VERSION(263, 266)
#define PC_VADDUHS BY_VERSION(264, 267)
#define PC_VADDUWM BY_VERSION(265, 268)
#define PC_VADDUWS BY_VERSION(266, 269)
#define PC_VAND BY_VERSION(267, 270)
#define PC_VANDC BY_VERSION(268, 271)
#define PC_VAVGSB BY_VERSION(269, 272)
#define PC_VAVGSH BY_VERSION(270, 273)
#define PC_VAVGSW BY_VERSION(271, 274)
#define PC_VAVGUB BY_VERSION(272, 275)
#define PC_VAVGUH BY_VERSION(273, 276)
#define PC_VAVGUW BY_VERSION(274, 277)
#define PC_VCFSX BY_VERSION(275, 278)
#define PC_VCFUX BY_VERSION(276, 279)
#define PC_VCMPBFP BY_VERSION(277, 280)
#define PC_VCMPEQFP BY_VERSION(278, 281)
#define PC_VCMPEQUB BY_VERSION(279, 282)
#define PC_VCMPEQUH BY_VERSION(280, 283)
#define PC_VCMPEQUW BY_VERSION(281, 284)
#define PC_VCMPGEFP BY_VERSION(282, 285)
#define PC_VCMPGTFP BY_VERSION(283, 286)
#define PC_VCMPGTSB BY_VERSION(284, 287)
#define PC_VCMPGTSH BY_VERSION(285, 288)
#define PC_VCMPGTSW BY_VERSION(286, 289)
#define PC_VCMPGTUB BY_VERSION(287, 290)
#define PC_VCMPGTUH BY_VERSION(288, 291)
#define PC_VCMPGTUW BY_VERSION(289, 292)
#define PC_VCTSXS BY_VERSION(290, 293)
#define PC_VCTUXS BY_VERSION(291, 294)
#define PC_VEXPTEFP BY_VERSION(292, 295)
#define PC_VLOGEFP BY_VERSION(293, 296)
#define PC_VMAXFP BY_VERSION(294, 297)
#define PC_VMAXSB BY_VERSION(295, 298)
#define PC_VMAXSH BY_VERSION(296, 299)
#define PC_VMAXSW BY_VERSION(297, 300)
#define PC_VMAXUB BY_VERSION(298, 301)
#define PC_VMAXUH BY_VERSION(299, 302)
#define PC_VMAXUW BY_VERSION(300, 303)
#define PC_VMINFP BY_VERSION(301, 304)
#define PC_VMINSB BY_VERSION(302, 305)
#define PC_VMINSH BY_VERSION(303, 306)
#define PC_VMINSW BY_VERSION(304, 307)
#define PC_VMINUB BY_VERSION(305, 308)
#define PC_VMINUH BY_VERSION(306, 309)
#define PC_VMINUW BY_VERSION(307, 310)
#define PC_VMRGHB BY_VERSION(308, 311)
#define PC_VMRGHH BY_VERSION(309, 312)
#define PC_VMRGHW BY_VERSION(310, 313)
#define PC_VMRGLB BY_VERSION(311, 314)
#define PC_VMRGLH BY_VERSION(312, 315)
#define PC_VMRGLW BY_VERSION(313, 316)
#define PC_VMULESB BY_VERSION(314, 317)
#define PC_VMULESH BY_VERSION(315, 318)
#define PC_VMULEUB BY_VERSION(316, 319)
#define PC_VMULEUH BY_VERSION(317, 320)
#define PC_VMULOSB BY_VERSION(318, 321)
#define PC_VMULOSH BY_VERSION(319, 322)
#define PC_VMULOUB BY_VERSION(320, 323)
#define PC_VMULOUH BY_VERSION(321, 324)
#define PC_VNOR BY_VERSION(322, 325)
#define PC_VOR BY_VERSION(323, 326)
#define PC_VPKPX BY_VERSION(324, 327)
#define PC_VPKSHSS BY_VERSION(325, 328)
#define PC_VPKSHUS BY_VERSION(326, 329)
#define PC_VPKSWSS BY_VERSION(327, 330)
#define PC_VPKSWUS BY_VERSION(328, 331)
#define PC_VPKUHUM BY_VERSION(329, 332)
#define PC_VPKUHUS BY_VERSION(330, 333)
#define PC_VPKUWUM BY_VERSION(331, 334)
#define PC_VPKUWUS BY_VERSION(332, 335)
#define PC_VREFP BY_VERSION(333, 336)
#define PC_VRFIM BY_VERSION(334, 337)
#define PC_VRFIN BY_VERSION(335, 338)
#define PC_VRFIP BY_VERSION(336, 339)
#define PC_VRFIZ BY_VERSION(337, 340)
#define PC_VRLB BY_VERSION(338, 341)
#define PC_VRLH BY_VERSION(339, 342)
#define PC_VRLW BY_VERSION(340, 343)
#define PC_VRSQRTEFP BY_VERSION(341, 344)
#define PC_VSL BY_VERSION(342, 345)
#define PC_VSLB BY_VERSION(343, 346)
#define PC_VSLH BY_VERSION(344, 347)
#define PC_VSLO BY_VERSION(345, 348)
#define PC_VSLW BY_VERSION(346, 349)
#define PC_VSPLTB BY_VERSION(347, 350)
#define PC_VSPLTH BY_VERSION(348, 351)
#define PC_VSPLTW BY_VERSION(349, 352)
#define PC_VSPLTISB BY_VERSION(350, 353)
#define PC_VSPLTISH BY_VERSION(351, 354)
#define PC_VSPLTISW BY_VERSION(352, 355)
#define PC_VSR BY_VERSION(353, 356)
#define PC_VSRAB BY_VERSION(354, 357)
#define PC_VSRAH BY_VERSION(355, 358)
#define PC_VSRAW BY_VERSION(356, 359)
#define PC_VSRB BY_VERSION(357, 360)
#define PC_VSRH BY_VERSION(358, 361)
#define PC_VSRO BY_VERSION(359, 362)
#define PC_VSRW BY_VERSION(360, 363)
#define PC_VSUBCUW BY_VERSION(361, 364)
#define PC_VSUBFP BY_VERSION(362, 365)
#define PC_VSUBSBS BY_VERSION(363, 366)
#define PC_VSUBSHS BY_VERSION(364, 367)
#define PC_VSUBSWS BY_VERSION(365, 368)
#define PC_VSUBUBM BY_VERSION(366, 369)
#define PC_VSUBUBS BY_VERSION(367, 370)
#define PC_VSUBUHM BY_VERSION(368, 371)
#define PC_VSUBUHS BY_VERSION(369, 372)
#define PC_VSUBUWM BY_VERSION(370, 373)
#define PC_VSUBUWS BY_VERSION(371, 374)
#define PC_VSUMSWS BY_VERSION(372, 375)
#define PC_VSUM2SWS BY_VERSION(373, 376)
#define PC_VSUM4SBS BY_VERSION(374, 377)
#define PC_VSUM4SHS BY_VERSION(375, 378)
#define PC_VSUM4UBS BY_VERSION(376, 379)
#define PC_VUPKHPX BY_VERSION(377, 380)
#define PC_VUPKHSB BY_VERSION(378, 381)
#define PC_VUPKHSH BY_VERSION(379, 382)
#define PC_VUPKLPX BY_VERSION(380, 383)
#define PC_VUPKLSB BY_VERSION(381, 384)
#define PC_VUPKLSH BY_VERSION(382, 385)
#define PC_VXOR BY_VERSION(383, 386)
#define PC_VMADDFP BY_VERSION(384, 387)
#define PC_VMHADDSHS BY_VERSION(385, 388)
#define PC_VMHRADDSHS BY_VERSION(386, 389)
#define PC_VMLADDUHM BY_VERSION(387, 390)
#define PC_VMSUMMBM BY_VERSION(388, 391)
#define PC_VMSUMSHM BY_VERSION(389, 392)
#define PC_VMSUMSHS BY_VERSION(390, 393)
#define PC_VMSUMUBM BY_VERSION(391, 394)
#define PC_VMSUMUHM BY_VERSION(392, 395)
#define PC_VMSUMUHS BY_VERSION(393, 396)
#define PC_VNMSUBFP BY_VERSION(394, 397)
#define PC_VPERM BY_VERSION(395, 398)
#define PC_VSEL BY_VERSION(396, 399)
#define PC_VSLDOI BY_VERSION(397, 400)
#define PC_VMR BY_VERSION(398, 401)
#define PC_VMRP BY_VERSION(399, 402)
#define PC_PSQ_L BY_VERSION(400, 403)
#define PC_PSQ_LU BY_VERSION(401, 404)
#define PC_PSQ_LX BY_VERSION(402, 405)
#define PC_PSQ_LUX BY_VERSION(403, 406)
#define PC_PSQ_ST BY_VERSION(404, 407)
#define PC_PSQ_STU BY_VERSION(405, 408)
#define PC_PSQ_STX BY_VERSION(406, 409)
#define PC_PSQ_STUX BY_VERSION(407, 410)
#define PC_DCBZ_L BY_VERSION(408, 411)
#define PC_PS_ADD BY_VERSION(409, 412)
#define PC_PS_SUB BY_VERSION(410, 413)
#define PC_PS_MUL BY_VERSION(411, 414)
#define PC_PS_DIV BY_VERSION(412, 415)
#define PC_PS_MADD BY_VERSION(413, 416)
#define PC_PS_MSUB BY_VERSION(414, 417)
#define PC_PS_NMADD BY_VERSION(415, 418)
#define PC_PS_NMSUB BY_VERSION(416, 419)
#define PC_PS_RES BY_VERSION(417, 420)
#define PC_PS_SEL BY_VERSION(418, 421)
#define PC_PS_ABS BY_VERSION(419, 422)
#define PC_PS_NABS BY_VERSION(420, 423)
#define PC_PS_NEG BY_VERSION(421, 424)
#define PC_PS_MR BY_VERSION(422, 425)
#define PC_PS_RSQRTE BY_VERSION(423, 426)
#define PC_PS_CMPU0 BY_VERSION(424, 427)
#define PC_PS_CMPO0 BY_VERSION(425, 428)
#define PC_PS_CMPU1 BY_VERSION(426, 429)
#define PC_PS_CMPO1 BY_VERSION(427, 430)
#define PC_PS_MERGE00 BY_VERSION(428, 431)
#define PC_PS_MERGE01 BY_VERSION(429, 432)
#define PC_PS_MERGE10 BY_VERSION(430, 433)
#define PC_PS_MERGE11 BY_VERSION(431, 434)
#define PC_PS_SUM0 BY_VERSION(432, 435)
#define PC_PS_SUM1 BY_VERSION(433, 436)
#define PC_PS_MULS0 BY_VERSION(434, 437)
#define PC_PS_MULS1 BY_VERSION(435, 438)
#define PC_PS_MADDS0 BY_VERSION(436, 439)
#define PC_PS_MADDS1 BY_VERSION(437, 440)
#define PC_SLE BY_VERSION(438, 441)
#define PC_SLEQ BY_VERSION(439, 442)
#define PC_SLIQ BY_VERSION(440, 443)
#define PC_SLLIQ BY_VERSION(441, 444)
#define PC_SLLQ BY_VERSION(442, 445)
#define PC_SLQ BY_VERSION(443, 446)
#define PC_SRAIQ BY_VERSION(444, 447)
#define PC_SRAQ BY_VERSION(445, 448)
#define PC_SRE BY_VERSION(446, 449)
#define PC_SREA BY_VERSION(447, 450)
#define PC_SREQ BY_VERSION(448, 451)
#define PC_SRIQ BY_VERSION(449, 452)
#define PC_SRLIQ BY_VERSION(450, 453)
#define PC_SRLQ BY_VERSION(451, 454)
#define PC_SRQ BY_VERSION(452, 455)
#define PC_MASKG BY_VERSION(453, 456)
#define PC_MASKIR BY_VERSION(454, 457)
#define PC_LSCBX BY_VERSION(455, 458)
#define PC_DIV BY_VERSION(456, 459)
#define PC_DIVS BY_VERSION(457, 460)
#define PC_DOZ BY_VERSION(458, 461)
#define PC_MUL BY_VERSION(459, 462)
#define PC_NABS BY_VERSION(460, 463)
#define PC_ABS BY_VERSION(461, 464)
#define PC_CLCS BY_VERSION(462, 465)
#define PC_DOZI BY_VERSION(463, 466)
#define PC_RLMI BY_VERSION(464, 467)
#define PC_RRIB BY_VERSION(465, 468)
#define PC_PENTRY BY_VERSION(466, 469)
#define PC_PEXIT BY_VERSION(467, 470)

/* A PCode instruction's flags (PCodeInstruction.flags), as the listing prints them (fIsConst, fIsVolatile, fSideEffects, ...); its
 * other bits are not named. */
#define fIsPtrOp BY_VERSION(0x40, (0x40) >> 2)
#define fSideEffects BY_VERSION(0x400, (0x400) >> 2)
#define fLink BY_VERSION(0x4000, (0x4000) >> 2)
#define fIsConst BY_VERSION(0x10000, (0x10000) >> 2)
#define fIsVolatile BY_VERSION(0x20000, (0x20000) >> 2)
#define fAbsolute BY_VERSION(0x40000, (0x40000) >> 2)
#define fOverflow BY_VERSION(0x80000, (0x80000) >> 2)
#define fCallerSPRelative BY_VERSION(0x400000, (0x400000) >> 2)
#define fIsLive BY_VERSION(0x800000, (0x800000) >> 2)

/* More of an instruction's flags, as the opcode table sets them per opcode: direct branches, loads, stores, record forms
 * (cr0), carry setters, register moves. */
#define fIsBranch BY_VERSION(0x4, (0x4) >> 2)
#define fIsRead BY_VERSION(0x8, (0x8) >> 2)
#define fIsWrite BY_VERSION(0x10, (0x10) >> 2)
#define fRecordBit BY_VERSION(0x80, 0x20000000)
#define PCODE_RECORD(f) (BY_VERSION((f) & fRecordBit, (((f) & 9 ? 0 : (f)) & 0x20000000)))
#define fSetsCarry BY_VERSION(0x100, (0x100) >> 2)
#define fIsMove BY_VERSION(0x800, (0x800) >> 2)

#pragma pack(push, 2)
struct PCodeInstruction {
    struct PCodeInstruction *next;
    struct PCodeInstruction *previous;
    struct PCodeBlock *block;
    int definitionStart;
    int useStart;
#if VERSION >= VERSION_GC_1_3
    unsigned int flags;
    int f18[2];
    short opcode;
    short operand_count;
#else
    short opcode;
    unsigned int flags;
    short operand_count;
#endif
    union {
        PCodeOperand operands[1];
#if VERSION >= VERSION_GC_1_3
        struct {
            unsigned char pad[26];
            struct PCodeLabel *label;
        } countLoopBranch;
        struct {
            unsigned char pad[26];
            short imm;
        } thirdImmediate;
        struct {
            unsigned char pad[26];
            int value;
        } thirdImmediateWord;
#endif
        struct PCodeAsmOperand {
            unsigned char kind;
            unsigned char arg;
            union {
                struct {
                    short reg;
                    int effect;
                } reg;
                struct {
                    int value;
                    struct Object *obj;
                } imm;
                struct {
                    int offset;
                    struct Object *obj;
                } mem;
                struct {
                    struct PCodeLabel *label;
                } label;
                struct {
                    short offset;
                    struct PCodeLabel *labelA;
                    struct PCodeLabel *labelB;
                } labeldiff;
            } data;
        } assemblyOperands[1];
    } operandData;
};
#pragma pack(pop)
#pragma pack(push, 2)
union PCodeBlockTarget {
    struct PCodeBlock *block;
    struct PCodeBlockLink *pendingLinks;
};
struct PCodeBlockLink {
    struct PCodeBlockLink *next;
    union PCodeBlockTarget payload;
};
#pragma pack(pop)
#pragma pack(push, 2)
struct PCodeBlock {
    struct PCodeBlock *next;
    struct PCodeBlock *prev;
    struct PCodeLabel *labels;
    PCodeBlockLink *predecessors;
    PCodeBlockLink *successors;
    PCodeInstruction *instructions;
    PCodeInstruction *reverse_instructions;
    int index;
#if VERSION < VERSION_GC_1_3
    int line;
#endif
    int code_offset;
#if VERSION >= VERSION_GC_1_3
    int execution_weight;
    short instruction_count;
    unsigned short flags;
    int line;
#else
    int execution_weight;
    short instruction_count;
    short flags;
#endif
};
#pragma pack(pop)
#pragma options align = mac68k
struct PCodeLabel {
    struct PCodeLabel *next;
    union PCodeBlockTarget target;
    UInt16 resolved;
    UInt16 number;
};
#pragma options align = reset
extern unsigned int data_00587ffc;
extern unsigned int PCode_SetCodeOffsets(void);
extern void Operands_AllocateGPR(unsigned int flags);
extern void PCode_InsertInstructionAfter(PCodeInstruction *h, PCodeInstruction *n);
extern void PCode_UnlinkInstruction(PCodeInstruction *instruction);
extern PCodeInstruction *PCode_AppendInstruction(PCodeBlock *block, PCodeInstruction *instruction);
extern void PCode_ResolveLabel(PCodeBlock *target, PCodeLabel *entry);
extern void PCode_UnlinkBlocksWithoutFlag4(void);
extern void PCode_BuildPredecessors(void);
extern void PCode_AddSuccessor(PCodeBlock *block, PCodeLabel *name);
extern PCodeBlock *PCode_CreateBlock(void);
extern void PCode_InsertInstructionBefore(PCodeInstruction *h, PCodeInstruction *n);
extern PCodeInstruction *PCode_CloneInstruction(PCodeInstruction *instr);
extern PCodeLabel *PCode_NewLabel(void);
extern void PCode_ResetBlocks(void);
#if VERSION >= VERSION_GC_1_3
extern void fn_00528030(PCodeInstruction *instr, short opcode);
extern void fn_00528090(PCodeInstruction *instr, int count);
#endif
#if VERSION >= VERSION_GC_1_3
#define PC_SETOPCODE(i, op) fn_00528030((i), (op))
#define PC_SETCOUNT(i, n) fn_00528090((i), (n))
#else
#define PC_SETOPCODE(i, op) ((i)->opcode = (op))
#define PC_SETCOUNT(i, n) ((i)->operand_count = (n))
#endif
extern void SpillCode_BuildBlockOrder(void);
extern struct PCodeBlock **gPCodeBlockOrder;

#ifdef __cplusplus
}
#endif

#endif
