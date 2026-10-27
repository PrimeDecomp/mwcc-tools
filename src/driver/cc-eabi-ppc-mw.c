#include "compiler/common.h"
#include "driver/cc-eabi-ppc-mw.h"
#include "driver/CLPlugins.h"
#include "driver/ClientGlue.h"
#include "driver/cc-eabi-ppc.h"

/* The static plugins the tool links in: the C/C++ compiler, a linker stub, the compiler's message strings and the
 * library importer. Each plugin's descriptions are its getters' data. */

#pragma scheduling off

static PluginDesc data_005434a8 = {2, 'Comp', 8, 0xB0000000, 'c++ ', 11};

int __stdcall get_name_and_length(char **name, int *length)
{
    *name = (char *)&data_005434a8;
    *length = 18;
    return 0;
}

static UInt32 data_005434bc = 'ePPC';
static UInt32 data_005434c0 = 'EABI';
static TargetInfo data_005434c4 = {1, 1, &data_005434bc, 1, &data_005434c0};

void *__stdcall set_next_to_global(struct ListNodeLink *node)
{
    node->next = (struct ListNodeLink *)&data_005434c4;
    return NULL;
}

static const char *data_005434e8 = "MW C/C++ PPC EABI";

unsigned int __stdcall copy_global_to_value(unsigned int *value)
{
    *value = (unsigned int)data_005434e8;
    return 0U;
}

static const char *data_00543500 = "Metrowerks C/C++";

unsigned int __stdcall get_stored_value(unsigned int *value)
{
    *value = (unsigned int)data_00543500;
    return 0;
}

static FileMap data_00543504[10] = {
    {'TEXT', ".c", 0},
    {'TEXT', ".c++", 0},
    {'TEXT', ".cc", 0},
    {'TEXT', ".cp", 0},
    {'TEXT', ".cpp", 0},
    {'TEXT', ".h", 0x10000000},
    {'TEXT', ".h++", 0x10000000},
    {'TEXT', ".hpp", 0x10000000},
    {'TEXT', ".pch", 0x80000000},
    {'TEXT', ".pch++", 0x80000000},
};
static FileMapInfo data_00543694 = {1, 10, data_00543504};

static void helper(signed char **p)
{
    *p = (signed char *)&data_00543694;
}

int __stdcall fn_0040be10(signed char **arguments)
{
    helper(arguments);
    return 0;
}

static const char *data_005436e8[4] = {"C/C++ Compiler", "C/C++ Warnings", "PPC EABI CodeGen", "EPPC Global Optimizer"};
static PluginDirectoryList data_005436f8 = {1, 4, (char **)data_005436e8};

unsigned int __stdcall fn_0040be20(struct ListLink *link)
{
    link->next = (struct ListLink *)&data_005436f8;
    return 0U;
}

static PluginVersion data_00543700 = {2, 3, 3, PLUGIN_BUILD};

unsigned int __stdcall fn_0040be30(struct ListLink *link)
{
    link->next = (struct ListLink *)&data_00543700;
    return 0;
}

static char data_00543704[] = "\276\357\372\316";
static char data_0054370c[] = "\316\372\357\276";
static FileSignature data_00543714[2] = {{'MMCH', data_00543704, 4, 0}, {'MMCH', data_0054370c, 4, 0}};
static FileSignatureList data_00543730 = {2, data_00543714};

void *__stdcall set_listnode_next_to_global(struct ListNode *node)
{
    node->next = (struct ListNode *)&data_00543730;
    return NULL;
}

static void *data_00543738[9] = {(void *)dispatch_compiler_plugin_request,
                                 (void *)get_name_and_length,
                                 (void *)get_stored_value,
                                 (void *)copy_global_to_value,
                                 (void *)fn_0040be20,
                                 NULL,
                                 NULL,
                                 (void *)fn_0040be30,
                                 (void *)set_listnode_next_to_global};

static CWObjectFlags data_00543774 = {2,      0x80000000, "o",    ".b",   "i",    "s",    "d",    "mch",  'CWIE',
                                      'ELF ', 'CWIE',     'BRWS', 'CWIE', 'TEXT', 'CWIE', 'TEXT', 'CWIE', 'TEXT'};

int __stdcall set_link_next_to_global(struct ListLink *link)
{
    link->next = (struct ListLink *)&data_00543774;
    return 0;
}

static void *data_005437bc[6] = {
    (void *)set_next_to_global, (void *)fn_0040be10, NULL, NULL, (void *)set_link_next_to_global, NULL};

static PluginDesc global_name = {2, 'Link', 8, 0x00000001, '****', 11};

int __stdcall get_global_name_and_length(char **name, int *length)
{
    *name = (char *)&global_name;
    *length = 18;
    return 0;
}
#pragma scheduling reset

#if VERSION >= VERSION_GC_1_3
unsigned char data_005437e8[] = "mwldnr2";
#endif
unsigned int __stdcall set_data_pointer(unsigned int objectAddress)
{
    struct DataPointerObject *object;
    unsigned char *data;
    object = (struct DataPointerObject *)objectAddress;
#if VERSION >= VERSION_GC_1_3
    object->data = (data = data_005437e8);
#else
    object->data = (data = (unsigned char *)"mwldnr2");
#endif
    object = NULL;
    return (unsigned int)object;
}

unsigned int __stdcall fn_0040be90(unsigned int objectAddress)
{
    struct TableObject *object;
    unsigned char *table;
    object = (struct TableObject *)objectAddress;
    object->table = (table = (unsigned char *)"Linker Tool Stub");
    object = NULL;
    return (unsigned int)object;
}

static const char *data_00543828[2] = {"PPC EABI Linker", "PPC EABI Project"};
static PluginDirectoryList data_00543830 = {1, 2, (char **)data_00543828};

#pragma optimization_level 2

unsigned int __stdcall fn_0040bea0(struct ListLink *link)
{
    link->next = (struct ListLink *)&data_00543830;
    return 0U;
}

#pragma optimization_level reset

static UInt32 data_00543838 = 'ePPC';
static UInt32 data_0054383c = 'EABI';
static TargetInfo data_00543840 = {1, 1, &data_00543838, 1, &data_0054383c};
static FileMap data_00543850[4] = {
    {0, ".doc", 0x10000000}, {'TEXT', ".lcf", 0}, {'TEXT', "", 0x10000000}, {0, ".bin", 0x20000000}};
static FileMapInfo data_005438f0 = {1, 4, data_00543850};

#pragma scheduling off

unsigned int __stdcall get_data_pointer(unsigned char **output)
{
    *output = (unsigned char *)&data_00543840;
    return 0;
}

unsigned int __stdcall set_shared_data(struct SharedDataHeader *header)
{
    header->data = (unsigned char *)&data_005438f0;
    return 0;
}

#pragma scheduling reset

static void *data_005438f8[9] = {NULL,
                                 (void *)get_global_name_and_length,
                                 (void *)fn_0040be90,
                                 (void *)set_data_pointer,
                                 (void *)fn_0040bea0,
                                 NULL,
                                 NULL,
                                 (void *)fn_0040be30,
                                 NULL};
static void *data_0054391c[6] = {(void *)get_data_pointer, (void *)set_shared_data, NULL, NULL, NULL, NULL};

unsigned int fn_0040bed0(void)
{
    unsigned int success = 0U;
    if (ClientGlue_CreateAndAddPlugin(data_00543738, data_005437bc) &&
        ClientGlue_CreateAndAddPlugin(data_005438f8, data_0054391c))
        success = 1U;
    return success;
}

static char *data_00545f68[] = {
    "illegal character constant",
    "illegal string constant",
    "unexpected end of file",
    "unterminated comment",
    "undefined preprocessor directive",
    "illegal token",
    "string too long",
    "identifier expected",
    "macro '%u' redefined",
    "illegal argument list",
    "too many macro arguments",
    "macro(s) too complex",
    "unexpected end of line",
    "end of line expected",
    "'(' expected",
    "')' expected",
    "',' expected",
    "preprocessor syntax error",
    "preceding #if is missing",
    "unterminated #if / macro",
    "unexpected token",
    "declaration syntax error",
    "identifier '%u' redeclared",
    "';' expected",
    "illegal constant expression",
    "']' expected",
    "illegal use of 'void'",
    "illegal function definition",
    "illegal function return type",
    "illegal array definition",
    "'}' expected",
    "illegal struct/union/enum/class definition",
    "struct/union/enum/class tag '%u' redefined",
    "struct/union/class member '%u' redefined",
    "declarator expected",
    "'{' expected",
    "illegal use of incomplete struct/union/class '%t'",
    "struct/union/class size exceeds 32K",
    "illegal bitfield declaration",
    "division by 0",
    "undefined identifier '%u'",
    "expression syntax error",
    "not an lvalue",
    "illegal operation",
    "illegal operand",
    "data type is incomplete",
    "illegal type",
    "too many initializers",
    "pointer/array required",
    "not a struct/union/class",
    "'%u' is not a struct/union/class member",
    "the file '%u' cannot be opened",
    "illegal instruction for this processor",
    "illegal operands for this processor",
    "number is out of range",
    "illegal addressing mode",
    "illegal data size",
    "illegal register list",
    "branch out of range",
    "undefined label '%u'",
    "reference to label '%u' is out of range",
    "call of non-function",
    "function call does not match prototype",
    "illegal use of register variable",
    "illegal type cast",
    "function already has a stackframe",
    "function has no initialized stackframe",
    "value is not stored in register",
    "function nesting too complex",
    "illegal use of keyword",
    "':' expected",
    "label '%u' redefined",
    "case constant defined more than once",
    "default label defined more than once",
    "illegal initialization",
    "illegal use of inline function",
    "illegal type qualifier(s)",
    "illegal storage class",
    "function has no prototype",
    "illegal assignment to constant",
    "illegal use of precompiled header",
    "illegal data in precompiled header",
    "variable / argument '%u' is not used in function",
    "illegal use of direct parameters",
    "return value expected",
    "variable '%u' is not initialized before being used",
    "illegal #pragma",
    "illegal access to protected/private member",
    "ambiguous access to class/struct/union member",
    "illegal use of 'this'",
    "unimplemented C++ feature",
    "illegal use of 'HandleObject'",
    "illegal access qualifier",
    "illegal 'operator' declaration",
    "illegal use of abstract class ('%o')",
    "illegal use of pure function",
    "illegal '&' reference",
    "illegal function overloading",
    "illegal operator overloading",
    "ambiguous access to overloaded function ",
    "illegal access/using declaration",
    "illegal 'friend' declaration",
    "illegal 'inline' function definition",
    "class has no default constructor",
    "illegal operator",
    "illegal default argument(s)",
    "possible unwanted ';'",
    "possible unwanted assignment",
    "possible unwanted compare",
    "illegal implicit conversion from '%t' to\n'%t'",
    "local data >32k",
    "illegal jump past initializer",
    "illegal ctor initializer",
    "cannot construct base class '%u'",
    "cannot construct direct member '%u'",
    "#if nesting overflow",
    "illegal empty declaration",
    "illegal implicit enum conversion from '%t' to\n'%t'",
    "illegal use of #pragma parameter",
    "virtual functions cannot be pascal functions",
    "illegal implicit const/volatile pointer conversion from '%t' to\n'%t'",
    "illegal use of non-static member",
    "illegal precompiled header version",
    "illegal precompiled header compiler flags or target",
    "'const' or '&' variable needs initializer",
    "'%o' hides inherited virtual function '%o'",
    "pascal function cannot be overloaded",
    "derived function differs from virtual base function in return type only",
    "non-const '&' reference initialized to temporary",
    "illegal template declaration",
    "'<' expected",
    "'>' expected",
    "illegal template argument(s)",
    "cannot instantiate '%o'",
    "template redefined",
    "template parameter mismatch",
    "cannot pass const/volatile data object to non-const/volatile member function",
    "preceding '#pragma push' is missing",
    "illegal explicit template instantiation",
    "illegal X::X(X) copy constructor",
    "function defined 'inline' after being called",
    "illegal constructor/destructor declaration",
    "'catch' expected",
    "#include nesting overflow",
    "cannot convert\n'%t' to\n'%t'",
    "type mismatch\n'%t' and\n'%t'",
    "class type expected",
    "illegal explicit conversion from '%t' to\n'%t'",
    "function call '*' does not match",
    "identifier '%u' redeclared\nwas declared as: '%t'\nnow declared as: '%t'",
    "cannot throw class with ambiguous base class ('%u')",
    "class '%t': '%o' has more than one final overrider:\n'%o'\nand '%o'",
    "exception handling option is disabled",
    "cannot delete pointer to const",
    "cannot destroy const object",
    "const member '%u' is not initialized",
    "'&' reference member '%u' is not initialized",
    "RTTI option is disabled",
    "constness casted away",
    "illegal const/volatile '&' reference initialization",
    "inconsistent linkage: 'extern' object redeclared as 'static'",
    "unknown assembler instruction mnemonic",
    "local data > 224 bytes",
    "'%u' could not be assigned to a register",
    "illegal exception specification",
    "exception specification list mismatch",
    "the parameter(s) of the '%n' function must be immediate value(s)",
    "SOM classes can only inherit from other SOM based classes",
    "SOM classes inhertiance must be virtual",
    "SOM class data members must be private",
    "illegal SOM function overload '%o'",
    "no static members allowed in SOM classes",
    "no parameters allowed in SOM class constructors",
    "illegal SOM function parameters or return type",
    "SOM runtime function '%u' not defined (should be defined in somobj.hh)",
    "SOM runtime function '%u' has unexpected type",
    "'%u' is not a SOM class",
    "illegal use of #pragma outside of SOM class definition",
    "introduced method '%o' is not specified in release order list",
    "SOM class access qualification only allowed to direct parent or own class",
    "SOM class must have one non-inline member function",
    "SOM type '%u' undefined",
    "new SOM callstyle method '%o' must have explicit 'Environment *' parameter",
    "functions cannot return SOM classes",
    "functions cannot have SOM class arguments",
    "assignment is not supported for SOM classes",
    "sizeof() is not supported for SOM classes",
    "SOM classes\tcannot be class members",
    "global SOM class objects are not supported",
    "SOM class arrays are not supported",
    "'pointer to member' is not supported for SOM classes",
    "SOM class has no release order list",
    "'%u' is not an Objective-C class",
    "method '%m' redeclared",
    "undefined method '%m'",
    "class '%u' redeclared",
    "class '%u' redefined",
    "Objective-C type '%u' is undefined (should be defined in objc.h)",
    "Objective-C type '%u' has unexpected type",
    "method '%m' not defined",
    "method '%m' redefined",
    "illegal use of 'self'",
    "illegal use of 'super'",
    "illegal message receiver",
    "receiver cannot handle this message",
    "ambiguous message selector\nused: '%m'\nalso had: '%m'",
    "unknown message selector",
    "illegal use of Objective-C object",
    "protocol '%u' redefined",
    "protocol '%u' is undefined",
    "protocol '%u' is already in protocol list",
    "category '%u' redefined",
    "category '%u' is undefined",
    "illegal use of '%u'",
    "template too complex or recursive",
    "illegal return value in void/constructor/destructor function",
    "assigning a non-int numeric value to an unprototyped function",
    "implicit arithmetic conversion from '%t' to '%t'",
    "preprocessor #error directive",
    "ambiguous access to name found '%u' and '%u'",
    "illegal namespace",
    "illegal use of namespace name",
    "illegal name overloading",
    "instance variable list does not match @interface",
    "protocol list does not match @interface",
    "super class does not match @interface",
    "function result is a pointer/reference to an automatic variable",
    "cannot allocate initialized objects in the scratchpad",
    "illegal class member access",
    "data object '%o' redefined",
    "illegal access to local variable from other function",
    "illegal implicit member pointer conversion",
    "typename redefined",
    "object '%o' redefined",
    "'main' not defined as external 'int main()' function",
    "illegal explicit template specialization",
    "name has not been declared in namespace/class",
    "preprocessor #warning directive",
    "illegal use of asm inline function",
    "illegal use of C++ feature in EC++",
    "illegal use template argument dependent type 'T::%u'",
    "illegal use of alloca() in function argument",
    "inline function call '%o' not inlined",
    "inconsistent use of 'class' and 'struct' keywords",
    "illegal partial specialization",
    "illegal partial specialization argument list",
    "ambiguous use of partial specialization",
    "local classes shall not have member templates",
    "illegal template argument dependent expression",
    "implicit 'int' is no longer supported in C++",
    "%i pad byte(s) inserted after data member '%u'",
    "pure function '%o' is not virtual",
    "illegal virtual function '%o' in 'union'",
    "cannot pass 'void' or 'function' parameter",
    "illegal static const member '%u' initialization",
    "'typename' is missing in template argument dependent qualified type",
    "more than one expression in non-class type conversion",
    "template non-type argument objects shall have external linkage",
    "illegal inline assembly operand: %u",
    "illegal or unsupported __attribute__",
    "cannot create object file '%f'",
    "error writing to object file '%f'",
    "printf-family format string doesn't match arguments",
    "scanf-family format string doesn't match arguments",
    "__alignof__() is not supported for SOM classes",
    "illegal macro argument name '%u'",
    "case has an empty range of values",
    "'long long' switch() is not supported",
    "'long long' case range is not supported",
    "expression has no side effect",
    "result of function call is not used",
    "illegal non-type template argument",
    NULL,
};

static char *data_00546598[] = {
    "### Error: Compilation aborted at end of file ###",
    "Save precompiled header as...",
    "### Error while creating precompiled headerfile (OSErr %ld) ###",
    "### Error while writing precompiled headerfile (OSErr %ld) ### ",
    "internal compiler error: File: '%s' Line: %ld",
    "ran out of registers--turn on Global Optimization for this function",
    "### Error: Out of memory ###",
    "### User break detected ###",
    "### Error: Cannot open main file ###",
    "Analyzing symbol table...",
    "Writing precompiled header file...",
    NULL,
};

static char *data_00547a34[] = {
    "ambiguous use of local variable(%n) and assembler register(%n) name",
    "ambiguous use of argument(%n) and assembler register(%n) name",
    "all registers are explictly used, can't color virtual '%u' registers",
    "parameter count to AltiVec intrinsic '%u' is %i, expected %i",
    "invalid parameter to AltiVec intrinsic '%u', %u( %t ) is not allowed",
    "invalid parameters to AltiVec intrinsic '%u', %u( %t,%t ) is not allowed",
    "invalid parameters to AltiVec intrinsic '%u', %u( %t, %t, %t ) is not allowed",
    "invalid parameter type to AltiVec intrinsic '%u'",
    "invalid constant parameter to AltiVec intrinsic '%u',\n'%u' last argument only accepts %i-bit constants",
    "label displacement is too far (must be within 32K bytes)",
    "too few initializers for '%t'",
    "too many initializers for '%t'",
    "illegal initialization of AltiVec vector data",
    "out of range for legal initialization of AltiVec '%t'",
    "illegal initialization or cast of AltiVec vector data",
    "use of AltiVec Model requires AltiVec-capable scheduler",
    "can't allocate AltiVec stack pointer, all registers already allocated (try optimize level 1 and higher)",
    "'%u' register not applicable to this processor",
    "only difference expressions are allowed on object\n (%u %u %u is not allowed)",
    "'%u' expressions is not allowed on an object\n(only %u +/- constant expression is allowed)",
    "'%u' operator is not allowed with an object\n(only constant expression + %u is allowed)",
    "only 2 objects are allowed in assembler expressions (%u, %u, %u)",
    "illegal object reference in constant expression (%u)",
    "illegal use of object difference (%u-%u)",
    "illegal can't mix labels and objects in expression (%u-%u)",
    "illegal use of label (%u), can only use label difference in this context",
    "illegal expression only one of {ha16, hi16, lo16, @h, @ha, @l} is allowed",
    "illegal use of register pair (%u) use %u@hiword or %u@loword",
    "cannot redefine uninitialized pooled data unless original definition is in common section",
    "identifier '%n' has already been defined in the '%n' section",
    "floating point type was checked in %u at line %i",
    "symbol '%n' would generate floating point instructions\n(HW floating point is off)",
    "processor settings don't support hardware floating point instructions",
    "invalid operand values: %i must be >= %i",
    "invalid operand values: %i + %i must be <= 32",
    "pragma section expected valid %u addressing mode",
    "pragma section expected unquoted %u addressing mode",
    "pragma section expected %u",
    "pragma section expected unquoted object type; found \"%u\"",
    "pragma section expected unquoted identifier for address mode; found \"%u\"",
    "invalid section name '%u'",
    "initialized sections must have uniquely named uninitialized data sections;\n'%u' is already defined for another section",
    "'sda_rel' addressing mode can only be used with the PPC EABI defined small data sections",
    "only use the initialized section name to refer to the section\n'%u' is the name of an uninitialized data section",
    "section '%u' already has uninitialized data section '%u'",
    "sections used for data must have an uninitialized data section",
    "section '%u' must have an uninitialized data section for object '%n'",
    "unknown section name '%u'",
    "pragma section expected a quoted section name",
    "'%u' parameter for pragma section is out of order",
    "'%u' parameter for pragma section is repeated",
    "unknown parameter '%u' for pragma section",
    "'%u' is only valid for '%u' addressing mode",
    "pragma section expected an object type or access permission and/or a quoted section name",
    "unknown or possibly out of order parameter '%u' for pragma section",
    "pragma section expected a quoted uninitialized section name\nor the unquoted identifiers 'data_mode', 'code_mode', 'R', 'RW', 'RX' or 'RWX'",
    "this pragma is ignored when it occurs within a function",
    "'%u' object '%n' is being put into section '%u' with access permission '%u';\nsection's access permission will be changed to '%u'",
    "pragma section expected an unquoted access permission; found \"%u\"",
    "pragma section expected object type or access permission, but not both",
    "pragma rel109_offset expected an integer (0, 1, 2 or 3)",
    "pragma function_align expected an integer (4, 8, 16, 32, 64 or 128)",
    "address part of section name '%u' is allowed 1 - 8 hexidecimal digits",
    "interrupt function '%o' is bigger than 256 bytes (%i bytes big)",
    "too many #pragma section directives (max is %i)",
    "EPPC Processor preference panel is incompatible with this compiler",
    "illegal forward label or undefined symbol (%u) in constant expression",
    "%u was not assigned to a register (try using register qualifier)",
    "@hiword can't be used in this context (only works on variables not registers)",
    "floating point constants are not allowed if floating point is off.",
    "possible unintended use of address of %u in constant expression\nUse 'la' or 'las' simplified mnemonics to load addresses",
    "expected a register name here",
    "out of registers for local variable %u\nTry using optimization level 1 or greater",
    "function level assembler can not be inlined.\nUse 'asm { instr... }' blocks inside an inline function.",
    "PCode cannot access the global variable %o, because it has been stored in the TOC.",
    "PCode cannot use long long.",
    "PCode function cannot take a variable argument list.",
    "Function '%u' contains no instructions.",
    NULL,
};

static char *data_00549c44[] = {
#include "driver/LinkerErrors.h"
};

#pragma scheduling off

unsigned int fn_0040bf10(void)
{
    unsigned int success;
    unsigned int thirdSucceeded;
    unsigned int secondSucceeded;
    success = 0U, thirdSucceeded = 0U, secondSucceeded = 0U;
    if (ClientGlue_AddResourceStrings("Compiler Errors", 10000, data_00545f68) != 0U) {
        if (ClientGlue_AddResourceStrings("Compiler Strings", 10100, data_00546598) != 0U) {
            secondSucceeded = 1U;
        }
    }
    if (secondSucceeded != 0U) {
        if (ClientGlue_AddResourceStrings("PPC Compiler Errors", 10001, data_00547a34) != 0U) {
            thirdSucceeded = 1U;
        }
    }
    if (thirdSucceeded != 0U) {
        if (ClientGlue_AddResourceStrings("Linker Errors", 11001, data_00549c44) != 0U) {
            success = 1U;
        }
    }
    return success;
}

#pragma scheduling reset
