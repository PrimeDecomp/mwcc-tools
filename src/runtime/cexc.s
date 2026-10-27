# The C++ runtime's exception table bounds (as the runtime library's cexc.obj has them): its start, in the first
# .exc section, and its end, in the last, a -1 the table's search stops at.
	.section .exc$AAA,"dr"
	.p2align 2
	.globl __PcToActionStart
__PcToActionStart:
	.section .exc$CCC,"dr"
	.p2align 2
	.globl __PcToActionEnd
__PcToActionEnd:
	.long -1
