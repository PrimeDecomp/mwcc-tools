# LMGR326B.dll's imports (written by tools/import_source.py from config/lmgr326b.def)
	.section .idata$2,"dw"
	.p2align 2
	.globl __IMPORT_DESCRIPTOR_LMGR326B
__IMPORT_DESCRIPTOR_LMGR326B:
	.rva .Llookup
	.long 0
	.long 0
	.rva .Ldll
	.rva .Laddresses
	.section .idata$4,"dw"
	.p2align 2
.Llookup:
	.long 0x800000bd
	.long 0x800000be
	.long 0x800000bf
	.long 0
	.section .idata$5,"dw"
	.p2align 2
.Laddresses:
	.globl __imp__lp_checkin
__imp__lp_checkin:
	.long 0x800000bd
	.globl __imp__lp_checkout
__imp__lp_checkout:
	.long 0x800000be
	.globl __imp__lp_errstring
__imp__lp_errstring:
	.long 0x800000bf
	.long 0
	.section .idata$6,"dw"
	.p2align 1
.Ldll:
	.asciz "LMGR326B.dll"
	.p2align 1
