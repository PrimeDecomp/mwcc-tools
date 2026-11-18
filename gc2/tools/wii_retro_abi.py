"""RC1 x86 build settings and vtable hook locations shared by both Wii builders."""

CLANG_FLAGS = (
    "--target=i686-pc-windows-msvc",
    "-O2",
    "-ffreestanding",
    "-fno-jump-tables",
    "-mno-sse",
    "-fno-asynchronous-unwind-tables",
    "-mstack-probe-size=65536",
    "-c",
)

VTABLE_CALL = 0x44F43E  # CParser_ParseDeclaration: call CMid_GlobalDeclarationActions
VTABLE_SYMBOLS = {
    "RC1_GlobalDeclarationActions": 0x47C120,
    "RC1_ConstructVTable": 0x552E50,
    "RC1_InitObjectIterator": 0x4F6550,
    "RC1_NextObjectIteratorObject": 0x4F64E0,
    "RC1_GenerateTypeInfoData": 0x600050,
    "RC1_ClassInternalError": 0x45B4C0,
    "RC1_CClass_source_file": 0x6C51D4,
    "RC1_frontend_errors": 0x76C0A0,
    "RC1_GenClassInstance": 0x5C1140,
    "RC1_FreeLocalHeap": 0x47C360,
    "RC1_classlist": 0x756FF4,
    "RC1_classlist2": 0x751D7C,
    "RC1_templates": 0x751EFC,
}
