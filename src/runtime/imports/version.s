# VERSION.dll's imports (written by tools/import_source.py from config/version.def)
	.section .idata$2,"dw"
	.p2align 2
	.globl __IMPORT_DESCRIPTOR_VERSION
__IMPORT_DESCRIPTOR_VERSION:
	.rva .Llookup
	.long 0
	.long 0
	.rva .Ldll
	.rva .Laddresses
	.section .idata$4,"dw"
	.p2align 2
.Llookup:
	.rva .Lname0
	.rva .Lname1
	.rva .Lname2
	.long 0
	.section .idata$5,"dw"
	.p2align 2
.Laddresses:
	.globl __imp__GetFileVersionInfoSizeA@8
__imp__GetFileVersionInfoSizeA@8:
	.rva .Lname0
	.globl __imp__GetFileVersionInfoA@16
__imp__GetFileVersionInfoA@16:
	.rva .Lname1
	.globl __imp__VerQueryValueA@16
__imp__VerQueryValueA@16:
	.rva .Lname2
	.long 0
	.section .idata$6,"dw"
	.p2align 1
.Lname0:
	.short 1
	.asciz "GetFileVersionInfoSizeA"
	.p2align 1
.Lname1:
	.short 0
	.asciz "GetFileVersionInfoA"
	.p2align 1
.Lname2:
	.short 10
	.asciz "VerQueryValueA"
	.p2align 1
.Ldll:
	.asciz "VERSION.dll"
	.p2align 1
