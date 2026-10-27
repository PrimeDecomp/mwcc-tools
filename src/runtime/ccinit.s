# The static initializers' table bounds (as the runtime library's ccinit.obj has them): a null word before the
# units' .CRT$XCU entries and one after, which the startup code runs between.
	.section .CRT$XCA,"dw"
	.p2align 2
	.globl ___xc_a
___xc_a:
	.long 0
	.section .CRT$XCZ,"dw"
	.p2align 2
	.globl ___xc_z
___xc_z:
	.long 0
