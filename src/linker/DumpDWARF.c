/* The DWARF 1 debugging information of -dis -show debug: the .debug section's entries (a length, a tag, then the
   attributes, each a name whose low 4 bits are its form, and a value), printed one per line, and converted to the
   other byte order on the way when the section's bytes are being converted.

   The same walk converts without printing: print is off when the linker converts an object's .debug only. */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"

#ifndef NONMATCHING /* (the wasm build has the target's stdarg.h) */
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + (((char *)(&(last) + 1) - (char *)&(last) + 3) / 4 * 4))
#define va_end(ap) ((void)0)
#endif

extern int vsprintf(char *s, const char *format, va_list arguments);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);

typedef void (*DumpDWARFPrinter)(char *text, long length, long size);
typedef void (*DumpDWARFAsserter)(const char *file, int line);

/* The section being walked, the context named in the bounds error, whether a value is read before its bytes are
   converted (else after), whether to print and to convert, the section's size and end, and the output and assertion
   procedures. */
static char *start;
static long context;
static char readBeforeConversion;
static char printing;
static char converting;
static unsigned long size;
static char *end;
static DumpDWARFPrinter printer;
static DumpDWARFAsserter asserter;

const char *DumpDWARF_TagName(unsigned short tag);
const char *DumpDWARF_AttributeName(short attribute);
const char *DumpDWARF_OperationName(unsigned char operation);
const char *DumpDWARF_FundamentalTypeName(short type, UInt16 *where);
double DumpDWARF_ReadDouble(unsigned char *bytes);
void DumpDWARF_Print(const char *format, ...);
long DumpDWARF_Block(char *data, short attribute);

/* A value's bytes must be in the section. LINE is the original's assertion line, in the message. */
static inline void CheckBounds(void *p, int line) {
    if ((start && (char *)p < start) || (char *)p > end) {
        CompilerTools_ReportLimitedDiagnostic(0x88, p, context, start, end, line, "DumpDWARF.c");
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
    }
}

/* A halfword, read and converted. */
static inline UInt16 ReadHalf(void *p) {
    UInt16 value;

    CheckBounds(p, 0x1d9);
    if (converting) {
        if (readBeforeConversion) {
            value = *(UInt16 *)p;
            CTool_EndianConvertInPlaceWord16Ptr(p);
        } else {
            CTool_EndianConvertInPlaceWord16Ptr(p);
            value = *(UInt16 *)p;
        }
    } else {
        value = *(UInt16 *)p;
    }
    return value;
}

/* A word, read and converted. */
static inline unsigned long ReadWord(void *p) {
    unsigned long value;

    CheckBounds(p, 0x1eb);
    if (converting) {
        if (readBeforeConversion) {
            value = *(unsigned long *)p;
            CTool_EndianConvertInPlaceWord32Ptr(p);
        } else {
            CTool_EndianConvertInPlaceWord32Ptr(p);
            value = *(unsigned long *)p;
        }
    } else {
        value = *(unsigned long *)p;
    }
    return value;
}

/* A halfword read before its bounds are checked (a tag or an attribute's name). */
static inline UInt16 ReadName(void *p, int line) {
    UInt16 value = *(UInt16 *)p;

    CheckBounds(p, line);
    if (converting) {
        CTool_EndianConvertInPlaceWord16Ptr(p);
        if (!readBeforeConversion)
            value = *(UInt16 *)p;
    } else {
        value = *(UInt16 *)p;
    }
    return value;
}

/* Walks the entries of DATA (SIZE bytes). */
/* 0x00453f90 */
void DumpDWARF(char *data, unsigned long dataSize, char print, char convert, DumpDWARFPrinter output,
               DumpDWARFAsserter assertion, char readFirst, long errorContext) {
    unsigned long pos;
    long length;
    long entryEnd;
    UInt16 tag;
    UInt16 attribute;
    unsigned long value;
    long count;
    UInt16 half;
    char *p;
    double real;

    start = data;
    end = data + dataSize;
    size = dataSize;
    context = errorContext;
    printing = print;
    converting = convert;
    asserter = assertion;
    printer = output;
    readBeforeConversion = readFirst;
    pos = 0;
    if (print)
        DumpDWARF_Print("\r\n********* DWARF Information *********\r\n");
    while (pos < dataSize) {
        entryEnd = pos;
        length = ReadWord(data + pos);
        if (length < 8) {
            if (printing)
                DumpDWARF_Print("%08x:<%ld>\r\n", pos, length);
            if (length <= 0)
                asserter("DumpDWARF.c", 0x311);
            pos += length;
            continue;
        }
        tag = ReadName(data + pos + 4, 0x22d);
        pos += 6;
        if (printing)
            DumpDWARF_Print("%08x:<%ld>%s\r\n", entryEnd, length, DumpDWARF_TagName(tag));
        if (tag == 0) {
            pos += length - 6;
            continue;
        }
        entryEnd += length;
        while ((long)pos < entryEnd) {
            attribute = ReadName(data + pos, 0x220);
            pos += 2;
            if (printing)
                DumpDWARF_Print("    %s", DumpDWARF_AttributeName(attribute));
            switch (attribute & 0xf) {
            case 1:
                if (printing) {
                    value = ReadWord(data + pos);
                    DumpDWARF_Print("(%08x)\r\n", value);
                }
                pos += 4;
                break;
            case 2:
                value = ReadWord(data + pos);
                if (printing)
                    DumpDWARF_Print("(%08x)\r\n", value);
                pos += 4;
                break;
            case 3:
                pos += DumpDWARF_Block(data + pos, attribute);
                if (printing)
                    DumpDWARF_Print("\r\n");
                break;
            case 4:
                count = ReadWord(data + pos);
                pos += 4;
                if (printing)
                    DumpDWARF_Print("(<%d>", count);
                if (attribute == 0xf4) {
                    while (count) {
                        if (printing) {
                            value = ReadWord(data + pos);
                            DumpDWARF_Print("(%x=%s)", value, data + pos + 4);
                        }
                        pos += 4;
                        count -= 4;
                        while (data[pos]) {
                            pos++;
                            count--;
                        }
                        pos++;
                        count--;
                    }
                }
                if (printing)
                    DumpDWARF_Print(")\r\n");
                break;
            case 5:
                p = data + pos;
                half = ReadHalf(p);
                pos += 2;
                if (attribute == 0x55) {
                    if (printing)
                        DumpDWARF_Print("(%s)\r\n", DumpDWARF_FundamentalTypeName(half, (UInt16 *)p));
                } else if (printing) {
                    DumpDWARF_Print("(%02x)\r\n", (short)half);
                }
                break;
            case 6:
                value = ReadWord(data + pos);
                pos += 4;
                if (attribute == 0x136) {
                    if (printing) {
                        const char *language;

                        switch ((unsigned char)value) {
                        case 1: language = "LANG_C89"; break;
                        case 2: language = "LANG_C"; break;
                        case 3: language = "LANG_ADA83"; break;
                        case 4: language = "LANG_C_PLUS_PLUS"; break;
                        case 5: language = "LANG_COBOL74"; break;
                        case 6: language = "LANG_COBOL85"; break;
                        case 7: language = "LANG_FORTRAN77"; break;
                        case 8: language = "LANG_FORTRAN90"; break;
                        case 9: language = "LANG_PASCAL83"; break;
                        case 10: language = "LANG_MODULA2"; break;
                        default: language = "LANG_<unknown>"; break;
                        }
                        DumpDWARF_Print("(%s)\r\n", language);
                    }
                } else if (printing) {
                    DumpDWARF_Print("(%08x)\r\n", value);
                }
                break;
            case 7:
                real = DumpDWARF_ReadDouble((unsigned char *)data + pos);
                pos += 8;
                if (printing)
                    DumpDWARF_Print("%G\r\n", real);
                break;
            case 8:
                if (printing)
                    DumpDWARF_Print("(");
                while (data[pos]) {
                    if (printing)
                        DumpDWARF_Print("%c", data[pos]);
                    pos++;
                }
                pos++;
                if (printing)
                    DumpDWARF_Print(")\r\n");
                break;
            default:
                if (printing)
                    DumpDWARF_Print("\r\n");
                break;
            }
        }
    }
    start = 0;
}

/* The modifier names of AT_mod_fund_type and AT_mod_u_d_type. */
static inline const char *ModifierName(unsigned char modifier) {
    switch (modifier) {
    case 1: return "MOD_pointer_to";
    case 2: return "MOD_reference_to";
    case 3: return "MOD_const";
    case 4: return "MOD_volatile";
    default: return "MOD_<unknown>";
    }
}

/* The subscript data's format names. */
static inline const char *FormatName(unsigned char format) {
    switch (format) {
    case 0: return "FMT_FT_C_C";
    case 1: return "FMT_FT_C_X";
    case 2: return "FMT_FT_X_C";
    case 3: return "FMT_FT_X_X";
    case 4: return "FMT_UT_C_C";
    case 5: return "FMT_UT_C_X";
    case 6: return "FMT_UT_X_C";
    case 7: return "FMT_UT_X_X";
    case 8: return "FMT_ET";
    default: return "FMT_<unknown>";
    }
}

/* A block attribute's value (its length, a halfword, then its bytes), by the attribute: a location expression's
   operations, the modifiers and type of AT_mod_fund_type (0x63) and AT_mod_u_d_type (0x83), the subscript data of
   AT_subscr_data (0xa3, whose bounds are themselves location expressions), else its bytes. It returns the bytes it
   used. The attribute (KIND) becomes an element type's attribute in the subscript data, which the bytes after it are
   then read as. */
/* 0x00454890 */
long DumpDWARF_Block(char *data, short kind) {
    long offset;
    short remaining;
    Boolean formatShown;
    UInt16 length;
    unsigned char op;
    unsigned long value;
    UInt16 half;
    char *p;
    long used;

    formatShown = 0;
    offset = 0;
    length = ReadHalf(data);
    remaining = length;
    offset += 2;
    if (printing)
        DumpDWARF_Print("(<%d>", length);
    while (remaining != 0) {
        op = data[offset];
        offset++;
        remaining--;
        if (kind == 0x23 || kind == 0x2a3 || kind == 0x2013 || kind == 0x293 || kind == 0x303 || kind == 0x2343) {
            /* a location expression */
            if (printing)
                DumpDWARF_Print(" %s", DumpDWARF_OperationName(op));
            switch (op) {
            case 1:
            case 2:
            case 4:
            case 0x80:
            case 0x81:
                value = ReadWord(data + offset);
                offset += 4;
                remaining -= 4;
                if (printing)
                    DumpDWARF_Print("(%ld)", value);
                break;
            case 3:
                value = ReadWord(data + offset);
                offset += 4;
                remaining -= 4;
                if (printing)
                    DumpDWARF_Print("(%08x)", value);
                break;
            case 0x93:
                value = (unsigned char)data[offset];
                offset++;
                remaining--;
                if (printing)
                    DumpDWARF_Print("(%0x)", value);
                break;
            case 0xe0:
                value = ReadWord(data + offset);
                offset += 4;
                remaining -= 4;
                if (printing)
                    DumpDWARF_Print("(%0x)", value);
                break;
            }
        } else if (kind == 0x83) {
            /* the modifiers, then the user-defined type's reference in the last 4 bytes */
            if ((unsigned short)remaining >= 4) {
                if (printing)
                    DumpDWARF_Print("%s ", ModifierName(op));
            } else {
                value = ReadWord(data + offset - 1);
                offset += 3;
                remaining -= 3;
                if (printing)
                    DumpDWARF_Print("(%06x)", value);
            }
        } else if (kind == 0x63) {
            /* the modifiers, then the fundamental type in the last 2 bytes */
            if ((unsigned short)remaining >= 2) {
                if (printing)
                    DumpDWARF_Print("%s ", ModifierName(op));
            } else {
                p = data + offset - 1;
                half = ReadHalf(p);
                offset++;
                remaining--;
                if (printing)
                    DumpDWARF_Print("%s", DumpDWARF_FundamentalTypeName(half, (UInt16 *)p));
            }
        } else if (kind == 0xa3) {
            /* the subscript data: a format, then its index type and bounds, or the element type */
            if (!formatShown) {
                if (printing)
                    DumpDWARF_Print(" %s: ", FormatName(op));
                formatShown = 1;
            }
            switch (op) {
            case 0:
                p = data + offset;
                half = ReadHalf(p);
                if (printing)
                    DumpDWARF_Print("%s ", DumpDWARF_FundamentalTypeName(half, (UInt16 *)p));
                value = ReadWord(data + offset + 2);
                if (printing)
                    DumpDWARF_Print("[%ld:", value);
                value = ReadWord(data + offset + 6);
                offset += 10;
                remaining -= 10;
                if (printing)
                    DumpDWARF_Print("%ld]", value);
                break;
            case 1:
                p = data + offset;
                half = ReadHalf(p);
                if (printing)
                    DumpDWARF_Print("%s ", DumpDWARF_FundamentalTypeName(half, (UInt16 *)p));
                value = ReadWord(data + offset + 2);
                offset += 6;
                remaining -= 6;
                if (printing)
                    DumpDWARF_Print("[%ld:", value);
                used = DumpDWARF_Block(data + offset, 0x23);
                offset += used;
                remaining -= used;
                if (printing)
                    DumpDWARF_Print("]");
                break;
            case 2:
                p = data + offset;
                half = ReadHalf(p);
                offset += 2;
                remaining -= 2;
                if (printing)
                    DumpDWARF_Print("%s ", DumpDWARF_FundamentalTypeName(half, (UInt16 *)p));
                if (printing)
                    DumpDWARF_Print("[");
                used = DumpDWARF_Block(data + offset, 0x23);
                offset += used;
                remaining -= used;
                value = ReadWord(data + offset);
                offset += 4;
                remaining -= 4;
                if (printing)
                    DumpDWARF_Print(":%ld]", value);
                break;
            case 3:
                p = data + offset;
                half = ReadHalf(p);
                offset += 2;
                remaining -= 2;
                if (printing)
                    DumpDWARF_Print("%s ", DumpDWARF_FundamentalTypeName(half, (UInt16 *)p));
                if (printing)
                    DumpDWARF_Print("[");
                used = DumpDWARF_Block(data + offset, 0x23);
                offset += used;
                remaining -= used;
                if (printing)
                    DumpDWARF_Print(":");
                used = DumpDWARF_Block(data + offset, 0x23);
                offset += used;
                remaining -= used;
                if (printing)
                    DumpDWARF_Print("]");
                break;
            case 4:
            case 5:
            case 6:
            case 7:
                formatShown = 0;
                kind = 0;
                break;
            case 8:
                /* the element type: an attribute and its value, the rest of the block */
                if (printing)
                    DumpDWARF_Print(" %s: ", FormatName(op));
                kind = ReadName(data + offset, 0x220);
                offset += 2;
                remaining -= 2;
                if (printing)
                    DumpDWARF_Print("%s", DumpDWARF_AttributeName(kind));
                if (kind == 0x55) {
                    p = data + offset;
                    half = ReadHalf(p);
                    offset += 2;
                    remaining -= 2;
                    if (printing)
                        DumpDWARF_Print("(%s)", DumpDWARF_FundamentalTypeName(half, (UInt16 *)p));
                    if (remaining != 0)
                        asserter("DumpDWARF.c", 0x2a9);
                } else if (kind == 0x63 || kind == 0x83) {
                    used = DumpDWARF_Block(data + offset, kind);
                    offset += used;
                    remaining -= used;
                    if (remaining != 0)
                        asserter("DumpDWARF.c", 0x2ad);
                } else if (kind == 0x72) {
                    value = ReadWord(data + offset);
                    if (printing)
                        DumpDWARF_Print("(%06x)", value);
                    offset += 4;
                    remaining -= 4;
                    if (remaining != 0)
                        asserter("DumpDWARF.c", 0x2b1);
                } else {
                    if (printing)
                        DumpDWARF_Print("Unknown element type: %ld\r\n", (unsigned short)kind);
                    asserter("DumpDWARF.c", 0x2b4);
                }
                formatShown = 0;
                break;
            }
        } else {
            if (printing)
                DumpDWARF_Print("%02x ", op);
        }
    }
    if (printing)
        DumpDWARF_Print(")");
    return offset;
}


/* A line of output, when printing. */
/* 0x00455950 */
void DumpDWARF_Print(const char *format, ...) {
    char text[0x13d0];
    va_list arguments;
    long length;

    if (!printing)
        return;
    va_start(arguments, format);
    length = vsprintf(text, format, arguments);
    va_end(arguments);
    printer(text, length, sizeof(text));
}

/* A double, read and converted. The conversion is of the pointer's own bytes, as in the original (not of the
   value's), so a value converted before it is read is read through the converted pointer. */
/* 0x004559d0 */
double DumpDWARF_ReadDouble(unsigned char *bytes) {
    union {
        double value;
        unsigned char bytes[8];
    } representation;

    CheckBounds(bytes, 0x200);
    if (converting) {
        if (readBeforeConversion) {
            representation.bytes[0] = bytes[0];
            representation.bytes[1] = bytes[1];
            representation.bytes[2] = bytes[2];
            representation.bytes[3] = bytes[3];
            representation.bytes[4] = bytes[4];
            representation.bytes[5] = bytes[5];
            representation.bytes[6] = bytes[6];
            representation.bytes[7] = bytes[7];
        }
        CTool_EndianConvertMem(&bytes, 8);
    }
    if (!readBeforeConversion) {
        representation.bytes[0] = bytes[0];
        representation.bytes[1] = bytes[1];
        representation.bytes[2] = bytes[2];
        representation.bytes[3] = bytes[3];
        representation.bytes[4] = bytes[4];
        representation.bytes[5] = bytes[5];
        representation.bytes[6] = bytes[6];
        representation.bytes[7] = bytes[7];
    }
    return representation.value;
}


/* A debugging entry's tag. */
/* 0x00456470 */
const char *DumpDWARF_TagName(unsigned short tag) {
    switch(tag) {
    case 0:
        return "TAG_padding";
    case 1:
        return "TAG_array_type";
    case 2:
        return "TAG_class_type";
    case 3:
        return "TAG_entry_point";
    case 4:
        return "TAG_enumeration_type";
    case 5:
        return "TAG_formal_parameter";
    case 6:
        return "TAG_global_subroutine";
    case 7:
        return "TAG_global_variable";
    default:
        return "TAG_<unknown>";
    case 10:
        return "TAG_label";
    case 0xb:
        return "TAG_lexical_block";
    case 0xc:
        return "TAG_local_variable";
    case 0xd:
        return "TAG_member";
    case 0xf:
        return "TAG_pointer_type";
    case 0x10:
        return "TAG_reference_type";
    case 0x11:
        return "TAG_compile_unit";
    case 0x12:
        return "TAG_string_type";
    case 0x13:
        return "TAG_structure_type";
    case 0x14:
        return "TAG_subroutine";
    case 0x15:
        return "TAG_subroutine_type";
    case 0x16:
        return "TAG_typedef";
    case 0x17:
        return "TAG_union_type";
    case 0x18:
        return "TAG_unspecified_parameters";
    case 0x19:
        return "TAG_variant";
    case 0x1a:
        return "TAG_common_block";
    case 0x1b:
        return "TAG_common_inclusion";
    case 0x1c:
        return "TAG_inheritance";
    case 0x1d:
        return "TAG_inlined_subroutine";
    case 0x1e:
        return "TAG_module";
    case 0x1f:
        return "TAG_ptr_to_member_type";
    case 0x20:
        return "TAG_set_type";
    case 0x21:
        return "TAG_subrange_type";
    case 0x22:
        return "TAG_with_stmt";
    }
}

/* An attribute's name and form. */
/* 0x00455f70 */
const char *DumpDWARF_AttributeName(short attribute) {
    if (attribute == 0x12) {
        return "AT_sibling";
    }
    if (attribute == 0x23) {
        return "AT_location";
    }
    if (attribute == 0x38) {
        return "AT_name";
    }
    if (attribute == 0x55) {
        return "AT_fund_type";
    }
    if (attribute == 99) {
        return "AT_mod_fund_type";
    }
    if (attribute == 0x72) {
        return "AT_user_def_type";
    }
    if (attribute == 0x83) {
        return "AT_mod_u_d_type";
    }
    if (attribute == 0x95) {
        return "AT_ordering";
    }
    if (attribute == 0xa3) {
        return "AT_subscr_data";
    }
    if (attribute == 0xb6) {
        return "AT_byte_size";
    }
    if (attribute == 0xc5) {
        return "AT_bit_offset";
    }
    if (attribute == 0xd6) {
        return "AT_bit_size";
    }
    if (attribute == 0xf4) {
        return "AT_element_list";
    }
    if (attribute == 0x106) {
        return "AT_stmt_list";
    }
    if (attribute == 0x111) {
        return "AT_low_pc";
    }
    if (attribute == 0x121) {
        return "AT_high_pc";
    }
    if (attribute == 0x136) {
        return "AT_language";
    }
    if (attribute == 0x142) {
        return "AT_member";
    }
    if (attribute == 0x152) {
        return "AT_discr";
    }
    if (attribute == 0x163) {
        return "AT_discr_value";
    }
    if (attribute == 0x193) {
        return "AT_string_length";
    }
    if (attribute == 0x1a2) {
        return "AT_common_reference";
    }
    if (attribute == 0x1b8) {
        return "AT_comp_dir";
    }
    if (attribute == 0x1c3) {
        return "AT_const_value_block2";
    }
    if (attribute == 0x1c4) {
        return "AT_const_value_block4";
    }
    if (attribute == 0x1c5) {
        return "AT_const_value_data2";
    }
    if (attribute == 0x1c6) {
        return "AT_const_value_data4";
    }
    if (attribute == 0x1c7) {
        return "AT_const_value_data8";
    }
    if (attribute == 0x1c8) {
        return "AT_const_value_string";
    }
    if (attribute == 0x1d2) {
        return "AT_containing_type";
    }
    if (attribute == 0x1e1) {
        return "AT_default_value_addr";
    }
    if (attribute == 0x1e5) {
        return "AT_default_value_data2";
    }
    if (attribute == 0x1e6) {
        return "AT_default_value_data4";
    }
    if (attribute == 0x1e7) {
        return "AT_default_value_data8";
    }
    if (attribute == 0x1e8) {
        return "AT_default_value_string";
    }
    if (attribute == 499) {
        return "AT_friends";
    }
    if (attribute == 0x208) {
        return "AT_inline";
    }
    if (attribute == 0x218) {
        return "AT_is_optional";
    }
    if (attribute == 0x222) {
        return "AT_lower_bound_ref";
    }
    if (attribute == 0x225) {
        return "AT_lower_bound_data2";
    }
    if (attribute == 0x226) {
        return "AT_lower_bound_data4";
    }
    if (attribute == 0x227) {
        return "AT_lower_bound_data8";
    }
    if (attribute == 0x238) {
        return "AT_program";
    }
    if (attribute == 0x248) {
        return "AT_private";
    }
    if (attribute == 600) {
        return "AT_producer";
    }
    if (attribute == 0x268) {
        return "AT_protected";
    }
    if (attribute == 0x278) {
        return "AT_prototyped";
    }
    if (attribute == 0x288) {
        return "AT_public";
    }
    if (attribute == 0x293) {
        return "AT_pure_virtual_block2";
    }
    if (attribute == 0x298) {
        return "AT_pure_virtual";
    }
    if (attribute == 0x2a3) {
        return "AT_return_addr";
    }
    if (attribute == 0x2b2) {
        return "AT_abstract_origin";
    }
    if (attribute == 0x2c6) {
        return "AT_start_scope";
    }
    if (attribute == 0x2e6) {
        return "AT_stride_size";
    }
    if (attribute == 0x2f2) {
        return "AT_upper_bound_ref";
    }
    if (attribute == 0x2f5) {
        return "AT_upper_bound_data2";
    }
    if (attribute == 0x2f6) {
        return "AT_upper_bound_data4";
    }
    if (attribute == 0x2f7) {
        return "AT_upper_bound_data8";
    }
    if (attribute == 0x303) {
        return "AT_virtual_block2";
    }
    if (attribute == 0x308) {
        return "AT_virtual";
    }
    if (attribute == 0x508) {
        return "AT_mangled";
    }
    if (attribute == 0x2008) {
        return "AT_mangled";
    }
    if (attribute == 0x2013) {
        return "AT_restore_SP";
    }
    if (attribute != 0x2022) {
        if (attribute == 0x2038) {
            return "AT_global_ref_by_name";
        }
        if (attribute == 0x2343) {
            return "AT_DWARF2_location";
        }
        if (attribute == -0x7ffa) {
            return "AT_source_file_names";
        }
        if (attribute == -0x7fea) {
            return "AT_src_info";
        }
        if (attribute == -0x7fda) {
            return "AT_mac_info";
        }
        if (attribute == -0x7fca) {
            return "AT_src_coords";
        }
        if (attribute == -0x7fbf) {
            return "AT_body_begin";
        }
        if (attribute != -0x7faf) {
            if (attribute != -0x7efa) {
                return "AT_<unknown>";
            }
            return "AT_source_info";
        }
        return "AT_body_end";
    }
    return "AT_global_ref";
}

/* A location expression's operation. */
/* 0x00455e40 */
const char *DumpDWARF_OperationName(unsigned char operation) {
    switch(operation) {
    case 1:
        return "OP_REG";
    case 2:
        return "OP_BASEREG";
    case 3:
        return "OP_ADDR";
    case 4:
        return "OP_CONST";
    case 5:
        return "OP_DEREF2";
    case 6:
        return "OP_DEREF4";
    case 7:
        return "OP_ADD";
    default:
        return "OP_<unknown>";
    case 0x50:
        return "DW_OP_reg0";
    case 0x51:
        return "DW_OP_reg1";
    case 0x52:
        return "DW_OP_reg2";
    case 0x53:
        return "DW_OP_reg3";
    case 0x54:
        return "DW_OP_reg4";
    case 0x55:
        return "DW_OP_reg5";
    case 0x56:
        return "DW_OP_reg6";
    case 0x57:
        return "DW_OP_reg7";
    case 0x58:
        return "DW_OP_reg8";
    case 0x59:
        return "DW_OP_reg9";
    case 0x5a:
        return "DW_OP_reg10";
    case 0x5b:
        return "DW_OP_reg11";
    case 0x5c:
        return "DW_OP_reg12";
    case 0x5d:
        return "DW_OP_reg13";
    case 0x5e:
        return "DW_OP_reg14";
    case 0x5f:
        return "DW_OP_reg15";
    case 0x60:
        return "DW_OP_reg16";
    case 0x61:
        return "DW_OP_reg17";
    case 0x62:
        return "DW_OP_reg18";
    case 99:
        return "DW_OP_reg19";
    case 100:
        return "DW_OP_reg20";
    case 0x65:
        return "DW_OP_reg21";
    case 0x66:
        return "DW_OP_reg22";
    case 0x67:
        return "DW_OP_reg23";
    case 0x68:
        return "DW_OP_reg24";
    case 0x69:
        return "DW_OP_reg25";
    case 0x6a:
        return "DW_OP_reg26";
    case 0x6b:
        return "DW_OP_reg27";
    case 0x6c:
        return "DW_OP_reg28";
    case 0x6d:
        return "DW_OP_reg29";
    case 0x6e:
        return "DW_OP_reg30";
    case 0x6f:
        return "DW_OP_reg31";
    case 0x80:
        return "OP_FPREG";
    case 0x81:
        return "OP_FPDREG";
    case 0x82:
        return "OP_DREF8";
    case 0x93:
        return "DW_OP_piece";
    }
}

/* A fundamental type; Metrowerks' long long types are rewritten at WHERE to their standard codes. */
/* 0x00455ad0 */
const char *DumpDWARF_FundamentalTypeName(short type, UInt16 *where) {
    if (type == 1) {
        return "FT_char";
    }
    if (type == 2) {
        return "FT_signed_char";
    }
    if (type == 3) {
        return "FT_unsigned_char";
    }
    if (type == 4) {
        return "FT_short";
    }
    if (type == 5) {
        return "FT_signed_short";
    }
    if (type == 6) {
        return "FT_unsigned_short";
    }
    if (type == 7) {
        return "FT_integer";
    }
    if (type == 8) {
        return "FT_signed_integer";
    }
    if (type == 9) {
        return "FT_unsigned_integer";
    }
    if (type == 10) {
        return "FT_long";
    }
    if (type == 0xb) {
        return "FT_signed_long";
    }
    if (type == 0xc) {
        return "FT_unsigned_long";
    }
    if (type == 0xd) {
        return "FT_pointer";
    }
    if (type == 0xe) {
        return "FT_float";
    }
    if (type == 0xf) {
        return "FT_dbl_prec_float";
    }
    if (type == 0x10) {
        return "FT_ext_prec_float";
    }
    if (type == 0x11) {
        return "FT_complex";
    }
    if (type == 0x12) {
        return "FT_dbl_prec_complex";
    }
    if (type == 0x14) {
        return "FT_void";
    }
    if (type == 0x15) {
        return "FT_boolean";
    }
    if (type == 0x16) {
        return "FT_ext_prec_complex";
    }
    if (type == 0x17) {
        return "FT_label";
    }
    if (type == 0x18) {
        *where = 0x8008;
        return "FT_MW_long_long";
    }
    if (type == 0x19) {
        *where = 0x8108;
        return "FT_MW_signed_long_long";
    }
    if (type == 0x1a) {
        *where = 0x8208;
        return "FT_MW_unsigned_long_long";
    }
    if (type == -0x7ff8) {
        return "FT_long_long";
    }
    if (type == -0x7ef8) {
        return "FT_signed_long_long";
    }
    if (type == -0x7df8) {
        return "FT_unsigned_long_long";
    }
    if (type == -0x6fff) {
        return "FT_int8";
    }
    if (type == -0x6eff) {
        return "FT_signed_int8";
    }
    if (type == -0x6dff) {
        return "FT_unsigned_int8";
    }
    if (type != -0x6cfe) {
        if (type == -0x6bfe) {
            return "FT_signed_int16";
        }
        if (type == -0x6afe) {
            return "FT_unsigned_int16";
        }
        if (type == -0x69fc) {
            return "FT_int32";
        }
        if (type == -0x68fc) {
            return "FT_signed_int32";
        }
        if (type == -0x67fc) {
            return "FT_unsigned_int32";
        }
        if (type == -0x66f8) {
            return "FT_int64";
        }
        if (type == -0x65f8) {
            return "FT_signed_int64";
        }
        if (type != -0x64f8) {
            if (type == -0x5ffc) {
                return "FT_real32";
            }
            if (type == -0x5ef8) {
                return "FT_real64";
            }
            if (type == -0x5df4) {
                return "FT_real96";
            }
            if (type != -0x5cf0) {
                if (type != -0x5400) {
                    return "FT_<unknown>";
                }
                return "FT_vec2x32float";
            }
            return "FT_real128";
        }
        return "FT_signed_int64";
    }
    return "FT_int16";
}
