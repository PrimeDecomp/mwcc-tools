.section .sdata2
.balign 8
.obj literal, local
.float 1.25
.endobj literal
.obj wide, local
.double -0.0000123456789
.endobj wide
.section .data
.balign 4
.obj blob, global
.4byte -1, 0x80000000, callee
.2byte 1, -2
.byte 0, 127, -1
.rel elsewhere, .
.string "line\n\x41\101\t"
.skip 3
.endobj blob
.obj gap_1, local
.skip 4
.endobj gap_1
.obj pointer, global
.4byte literal
.endobj pointer
.rodata
.balign 4
.obj "@stringBase0", local
.string "alpha"
.string "beta"
.skip 2
.endobj "@stringBase0"
.bss
.balign 16
.obj buffer, local
.skip 16
.endobj buffer
.comm shared, 16
.text
.fn "use literals", global
lis r3, literal@ha
addi r3, r3, literal@l
lfs f1, wide@sda21(r2)
.endfn "use literals"
