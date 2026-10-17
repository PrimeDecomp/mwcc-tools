#ifndef COMPILER_PPCERROR_H
#define COMPILER_PPCERROR_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// the back end's messages (2.6's STR# 10001, from cc-eabi-ppc.r)
enum {
    // "ambiguous use of local variable(%n) and assembler register(%n) name"
    PPCErrorStr100 = 100,
    // "ambiguous use of argument(%n) and assembler register(%n) name"
    PPCErrorStr101 = 101,
    // "all registers are explictly used, can't color virtual '%u' registers"
    PPCErrorStr102 = 102,
    // "parameter count to AltiVec intrinsic '%u' is %i, expected %i"
    PPCErrorStr103 = 103,
    // "invalid parameter to AltiVec intrinsic '%u', %u( %t ) is not allowed"
    PPCErrorStr104 = 104,
    // "invalid parameters to AltiVec intrinsic '%u', %u( %t,%t ) is not allowed"
    PPCErrorStr105 = 105,
    // "invalid parameters to AltiVec intrinsic '%u', %u( %t, %t, %t ) is not allowed"
    PPCErrorStr106 = 106,
    // "invalid parameter type to AltiVec intrinsic '%u'"
    PPCErrorStr107 = 107,
    // "invalid constant parameter to AltiVec intrinsic '%u',\n'%u' last argument only accepts %i-bit constants"
    PPCErrorStr108 = 108,
    // "label displacement is too far (must be within 32K bytes)"
    PPCErrorStr109 = 109,
    // "too few initializers for '%t'"
    PPCErrorStr110 = 110,
    // "too many initializers for '%t'"
    PPCErrorStr111 = 111,
    // "illegal initialization of AltiVec vector data"
    PPCErrorStr112 = 112,
    // "out of range for legal initialization of AltiVec '%t'"
    PPCErrorStr113 = 113,
    // "illegal initialization or cast of AltiVec vector data"
    PPCErrorStr114 = 114,
    // "use of AltiVec Model requires AltiVec-capable scheduler"
    PPCErrorStr115 = 115,
    // "can't allocate AltiVec stack pointer, all registers already allocated (try optimize level 1 and higher)"
    PPCErrorStr116 = 116,
    // "'%u' register not applicable to this processor"
    PPCErrorStr117 = 117,
    // "only difference expressions are allowed on object\n (%u %u %u is not allowed)"
    PPCErrorStr118 = 118,
    // "'%u' expressions is not allowed on an object\n(only %u +/- constant expression is allowed)"
    PPCErrorStr119 = 119,
    // "'%u' operator is not allowed with an object\n(only constant expression + %u is allowed)"
    PPCErrorStr120 = 120,
    // "only 2 objects are allowed in assembler expressions (%u, %u, %u)"
    PPCErrorStr121 = 121,
    // "illegal object reference in constant expression (%u)"
    PPCErrorStr122 = 122,
    // "illegal use of object difference (%u-%u)"
    PPCErrorStr123 = 123,
    // "illegal can't mix labels and objects in expression (%u-%u)"
    PPCErrorStr124 = 124,
    // "illegal use of label (%u), can only use label difference in this context"
    PPCErrorStr125 = 125,
    // "illegal expression only one of {ha16, hi16, lo16, @h, @ha, @l} is allowed"
    PPCErrorStr126 = 126,
    // "illegal use of register pair (%u) use %u@hiword or %u@loword"
    PPCErrorStr127 = 127,
    // "cannot redefine uninitialized pooled data unless original definition is in common section"
    PPCErrorStr128 = 128,
    // "identifier '%n' has already been defined in the '%n' section"
    PPCErrorStr129 = 129,
    // "floating point type was checked in %u at line %i"
    PPCErrorStr130 = 130,
    // "symbol '%n' would generate floating point instructions\n(HW floating point is off)"
    PPCErrorStr131 = 131,
    // "processor settings don't support hardware floating point instructions"
    PPCErrorStr132 = 132,
    // "invalid operand values: %i must be >= %i"
    PPCErrorStr133 = 133,
    // "invalid operand values: %i + %i must be <= 32"
    PPCErrorStr134 = 134,
    // "pragma section expected valid %u addressing mode"
    PPCErrorStr135 = 135,
    // "pragma section expected unquoted %u addressing mode"
    PPCErrorStr136 = 136,
    // "pragma section expected %u"
    PPCErrorStr137 = 137,
    // "pragma section expected unquoted object type; found \"%u\""
    PPCErrorStr138 = 138,
    // "pragma section expected unquoted identifier for address mode; found \"%u\""
    PPCErrorStr139 = 139,
    // "invalid section name '%u'"
    PPCErrorStr140 = 140,
    // "initialized sections must have uniquely named uninitialized data sections;\n'%u' is already defined for another section"
    PPCErrorStr141 = 141,
    // "'sda_rel' addressing mode can only be used with the PPC EABI defined small data sections"
    PPCErrorStr142 = 142,
    // "only use the initialized section name to refer to the section\n'%u' is the name of an uninitialized data section"
    PPCErrorStr143 = 143,
    // "section '%u' already has uninitialized data section '%u'"
    PPCErrorStr144 = 144,
    // "sections used for data must have an uninitialized data section"
    PPCErrorStr145 = 145,
    // "section '%u' must have an uninitialized data section for object '%n'"
    PPCErrorStr146 = 146,
    // "unknown section name '%u'"
    PPCErrorStr147 = 147,
    // "pragma section expected a quoted section name"
    PPCErrorStr148 = 148,
    // "'%u' parameter for pragma section is out of order"
    PPCErrorStr149 = 149,
    // "'%u' parameter for pragma section is repeated"
    PPCErrorStr150 = 150,
    // "unknown parameter '%u' for pragma section"
    PPCErrorStr151 = 151,
    // "'%u' is only valid for '%u' addressing mode"
    PPCErrorStr152 = 152,
    // "pragma section expected an object type or access permission and/or a quoted section name"
    PPCErrorStr153 = 153,
    // "unknown or possibly out of order parameter '%u' for pragma section"
    PPCErrorStr154 = 154,
    // "pragma section expected a quoted uninitialized section name\nor the unquoted identifiers 'data_mode', 'code_mode', 'R', 'RW', 'RX' or 'RWX'"
    PPCErrorStr155 = 155,
    // "this pragma is ignored when it occurs within a function"
    PPCErrorStr156 = 156,
    // "'%u' object '%n' is being put into section '%u' with access permission '%u';\nsection's access permission will be changed to '%u'"
    PPCErrorStr157 = 157,
    // "pragma section expected an unquoted access permission; found \"%u\""
    PPCErrorStr158 = 158,
    // "pragma section expected object type or access permission, but not both"
    PPCErrorStr159 = 159,
    // "pragma rel109_offset expected an integer (0, 1, 2 or 3)"
    PPCErrorStr160 = 160,
    // "pragma function_align expected an integer (4, 8, 16, 32, 64 or 128)"
    PPCErrorStr161 = 161,
    // "address part of section name '%u' is allowed 1 - 8 hexidecimal digits"
    PPCErrorStr162 = 162,
    // "interrupt function '%o' is bigger than 256 bytes (%i bytes big)"
    PPCErrorStr163 = 163,
    // "too many #pragma section directives (max is %i)"
    PPCErrorStr164 = 164,
    // "EPPC Processor preference panel is incompatible with this compiler"
    PPCErrorStr165 = 165,
    // "illegal forward label or undefined symbol (%u) in constant expression"
    PPCErrorStr166 = 166,
    // "%u was not assigned to a register (try using register qualifier)"
    PPCErrorStr167 = 167,
    // "@hiword can't be used in this context (only works on variables not registers)"
    PPCErrorStr168 = 168,
    // "floating point constants are not allowed if floating point is off."
    PPCErrorStr169 = 169,
    // "possible unintended use of address of %u in constant expression\nUse 'la' or 'las' simplified mnemonics to load addresses"
    PPCErrorStr170 = 170,
    // "expected a register name here"
    PPCErrorStr171 = 171,
    // "out of registers for local variable %u\nTry using optimization level 1 or greater"
    PPCErrorStr172 = 172,
    // "function level assembler can not be inlined.\nUse 'asm { instr... }' blocks inside an inline function."
    PPCErrorStr173 = 173,
    // "PCode cannot access the global variable %o, because it has been stored in the TOC."
    PPCErrorStr174 = 174,
    // "PCode cannot use long long."
    PPCErrorStr175 = 175,
    // "PCode function cannot take a variable argument list."
    PPCErrorStr176 = 176,
    // "illegal use of label or object in a absolute branch."
    PPCErrorStr177 = 177,
    // "AltiVec PIM requires warning for volatile parameter to %u."
    PPCErrorStr178 = 178,
    // "Mach-O can't load a float constant directly. Use addis rX,picbase,ha16(float_const_var); lfd fX,lo16(float_const_var)(rX)"
    PPCErrorStr179 = 179,
    // "Illegal addressing mode (32 bit). Try constructing an address using addis rX,rPIC,ha16(var); lwz rX,lo16(var)(rX)"
    PPCErrorStr180 = 180,
    // "%o cannot be an interrupt function with variable argument list"
    PPCErrorStr181 = 181,
    // "incoming parameter area > 32K"
    PPCErrorStr182 = 182,
    // "outgoing parameter area > 32K"
    PPCErrorStr183 = 183,
    // "N/A"
    PPCErrorStr184 = 184,
    // "when there is a frame, it is best to have frfree just before the %u"
    PPCErrorStr185 = 185,
    // "when there is a frame, it is best to have only one explicit exit\n(try branching to a label just before the frfree)"
    PPCErrorStr186 = 186,
    // "when there is a frame, it is generally best to have a %u just after the frfree"
    PPCErrorStr187 = 187,
    // "frfree directive is only allowed at the end of a function. To exit from the middle of a function, branch to a frfree at the end."
    PPCErrorStr188 = 188,
    // "Mach-O 'pic' model is only supported if 'dynamic' is on."
    PPCErrorStr189 = 189,
    // "Function '%u' contains no instructions."
    PPCErrorStr190 = 190,
    // "pragma min_struct_alignment expected an integer (4, 8, 16, 32, 64 or 128)."
    PPCErrorStr191 = 191,
    // "correct syntax is __declspec(section \"sect_name\") where sect_name is a quoted string."
    PPCErrorStr192 = 192,
    // "global register variable declarations are ignored when they occur within a function."
    PPCErrorStr193 = 193,
    // "global register variable declarations must be variables of storage class static, extern or register."
    PPCErrorStr194 = 194,
    // "global register variable declaration syntax is storage_class type var_name asm (\"xn\");\nstorage_class is extern, static or register, \"xn\" is quoted, 'x' should be r,f or v,\nand 'n' should be a nonvolatile register number."
    PPCErrorStr195 = 195,
    // "global register variable declaration supports %i registers beginning with %u%i for this register class."
    PPCErrorStr196 = 196,
    // "global register variable assigned to '%u%i' can not be reassigned to '%u%i'."
    PPCErrorStr197 = 197,
    // "global register variable '%n' already is assigned to '%u%i'."
    PPCErrorStr198 = 198,
    // "gaps between assigned global register variables (nothing at '%u%i') are not permitted."
    PPCErrorStr199 = 199,
    // "global register variable is assigned to register class '%u' but should be '%u'."
    PPCErrorStr200 = 200,
    // "global register variables can only use single registers (cannot be long longs or software fp doubles)."
    PPCErrorStr201 = 201,
    // "global register variable '%o' cannot be assigned %u%i because that register has already been assigned to something else."
    PPCErrorStr202 = 202,
    // "global and static register variables cannot have the same name: '%n'."
    PPCErrorStr203 = 203,
    // "global register variables must be variables; '%n' isn't a variable."
    PPCErrorStr204 = 204,
    // "type cannot be made into a global register variable;\nonly scalers, doubles, floats and vectors are supported."
    PPCErrorStr205 = 205,
    // "cannot declare global register variables after code has been generated."
    PPCErrorStr206 = 206,
    // "correct syntax is __declspec(interrupt flags) where optional flags can be 1 or more of following space separated list:\nenable SRR DAR DSISR fprs vrs nowarn."
    PPCErrorStr207 = 207,
    // "pragma processor expected an integer, such as 603, or a string (no quotes), such as 603e."
    PPCErrorStr208 = 208,
    // "pragma prepare_compress is ignored if an appropriate processor is not passed to pragma processor."
    PPCErrorStr209 = 209,
    // "intrinsic %u argument %i is not constant"
    PPCErrorStr210 = 210,
    // "intrinsic %u argument %i constant value value %i is not in range %i..%i using %i"
    PPCErrorStr211 = 211,
    // "inline asm instruction '%u', defines reserved register r%i.\nMixing assembler and C code depends on the inline assembler not modifying the reserved registers directly.\nUse function level assembler if you need to directly modify the reserved registers."
    PPCErrorStr212 = 212,
    // "out of range shift value (%i) will be masked to (%i)."
    PPCErrorStr213 = 213,
    // "function prototype requires an ellipsis for variable argument macro va_start."
    PPCErrorStr214 = 214,
    // "illegal use of frame pointer (r%i) in statement level assembler"
    PPCErrorStr215 = 215,
    // "use pragma %u after pragma prepare_compress"
    PPCErrorStr216 = 216,
    // "branch instruction needs to branch %i but range is %i ... %i"
    PPCErrorStr217 = 217,
    // "currently cannot mix compressed and uncompressed executable sections in the same process image"
    PPCErrorStr218 = 218,
    // "function consumes too many registers; use global optimization level >= 1 and/or reduce inlining"
    PPCErrorStr219 = 219,
    PPCErrorStrMAX = 220
};

extern void PPCError_Error(int code, ...);
extern void PPCError_Warning(int code, ...);
extern void PPCError_Message(char* format, ...);
extern void PPCError_ErrorTerm(short code, ...);

#ifdef __cplusplus
}
#endif

#endif
