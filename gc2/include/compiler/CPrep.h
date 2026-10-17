#ifndef COMPILER_CPREP_H
#define COMPILER_CPREP_H

#include "compiler/common.h"
#include "compiler/tokens.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/Files.h"

#ifdef __cplusplus
extern "C" {
#endif

// (Pro7: mac68k alignment)
#pragma pack(push, 2)
extern SInt16 tk;
extern GList pplist;
// an option's key for CPrep_PushOption and CPrep_PopOption: its offset in copts
#define OPT_OFFSET(optname) ((short)&((CompilerLinkerOptions*)0)->optname)
#define OPT_OFFSET_MASK 0x1FFF
// (Pro7: tokens.h) the bytes a macro's stored text marks its arguments with
enum {
    TK_MACRO_ARG_1 = 1,
    TK_MACRO_ARG_2 = 2,
    TK_MACRO_ARG_HASH = 3, // #arg, followed by the argument's index
    TK_4 = 4,
    TK_5 = 5
};
#define OPT_FLAG_2000 0x2000
#define OPT_FLAG_4000 0x4000
#define OPT_FLAG_8000 0x8000
// (Pro7's common.h) what the compiler plugin's entry gives the front end (cparams, CCompiler.c)
struct CompilerLinkerParamBlk {
    struct CWPluginPrivateContext* context;
    CWObjectData object;
    Handle objectdata;
    Handle browsedata;
    SInt32 request;
    SInt32 version;
    CWFileSpec targetfile;
    SInt32 numfiles;
    SInt32 whichfile;
    CWFileSpec sourcefile;
    const char* sourcetext;
    SInt32 sourcetextsize;
    Boolean precompile;
    Boolean autoprecompile;
    Boolean preprocess;
    Boolean SYMinfo;
    Boolean caching_includes;
    CWBrowseOptions browseoptions;
    Boolean recordbrowseinfo;
    SInt16 browserfileID;
    SInt32 targetOS;
    SInt32 targetCPU;
    char* idetargetname;
};
struct Macro {
    struct Macro* next;
    struct HashNameNode* name;
    // TODO: invented names (Pro7: c, xC, xF) for text, nargs and isExpanding
    char* text;
    UInt16 nargs;
    Boolean is_special;
    UInt8 isExpanding; // (ismacroname2 rejects active expansions; CPrep_PopState clears it)
    // 2.6's special macros (envM, trgtM ...) are 20-byte statics: a one-element array where Pro7 has names[0]
    struct HashNameNode* names[1];
};
struct TokenStack {
    char* pos;
    char* macrostart;
    struct Macro* macro;
    Boolean macrocheck;
};
struct CPrepFileInfo {
    CWFileSpec textfile;
    // the file's path (#line's file once one names it)
    struct HashNameNode* nameNode;
    SInt32 fileModDate;
    // TODO: invented name (2.6): the file's real line minus #line's, set by prepline
    SInt32 linedelta;
    char* textbuffer;
    SInt32 textlength;
    SInt32 linenumber;
    SInt32 pos;
    Boolean hasprepline;
    SInt16 fileID;
    Boolean recordbrowseinfo;
    // TODO: Pro7's placeholder names: setupfile clears them with the rest, nothing reads them
    Boolean unkfield123;
    Boolean unkfield124;
    Boolean unkfield125;
    // (Pro7: unkfield126) TODO: invented name: setupfile sets it for the default prefix file
    Boolean isDefault;
};
#pragma pack(pop)
extern CompilerLinkerParamBlk* cparamblkptr;
extern CInt64 tkintconst;
extern struct Float tkfloatconst;
extern char* tkstring;
extern struct HashNameNode* tkidentifier;
extern SInt32 tksize;
extern UInt16 ispascalstring;
extern SInt16 nlflag;
extern SInt32 lines;
extern UInt8 spaceskip;
extern struct Macro** macrohashtable;
extern UInt8 cprep_nomem_exit;
extern UInt8 cprep_nostring;
extern UInt8 cprep_eoltokens;
extern TokenStack tokenstack[128];
extern SInt16 tokenstacklevel;
extern char* pos;
extern char* macropos;
extern char* nextcharpos;
extern unsigned char CPrep_SkipNewCommentChar;
extern UInt8 preprocessing_only;
extern Handle stringmem;
extern SInt32 maxstringsize;
extern UInt8 cprep_idarray[256];
extern UInt8 was_escchar;
extern UInt8 macrocheck;
extern UInt8 widestring;
extern unsigned char at_linestart;
extern char* prep_file_start;
extern char* prep_file_end;
extern UInt8 cprep_strconcat;
extern struct CPrepFileInfo* prep_file;
extern short filesp;
extern int linenumber;
extern struct TStreamElement* ts_last;
extern struct TStreamElement* ts_current;
extern int ts_preread_elements;

extern void CPrep_InsertSpecialMacro(Macro* macro, const char* name);
extern void CPrep_InsertSpecialMacros(void);
extern void CPrep_RemoveSpecialMacros(void);
extern void CPrep_TSBufferGrow(SInt32 amount);
extern void CPrep_TokenStreamGetState(SInt32* state);
extern void CPrep_TokenStreamSetState(SInt32* state);
extern void CPrep_UnLex(void);
extern void CPrep_TokenStreamSetCurState(SInt32* state);
extern void CPrep_StreamGetBlock(TokenStream* stream, void (*func)(TStreamElement*), int arg);
extern void CPrep_StreamGetSemicolon(TokenStream* stream, void (*func)(TStreamElement*));
extern void CPrep_StreamInsert(TokenStream* stream, SInt32* state);
extern void CPrep_StreamRemove(TokenStream* stream, SInt32* state);
extern void CPrep_TokenStreamFlush(void);
extern TStreamElement* CPrep_CurStreamElement(void);
extern void CPrep_Error(short code);
extern void CPrep_ErrorName(short code, const char* name);
extern void CPrep_Warning(short code);
extern void popfile(void);
extern void CPrep_SetSourceFile(FileOffsetInfo* foi);
extern void CPrep_NewFileOffsetInfo(FileOffsetInfo* foi, TStreamElement* ts);
extern SInt32 CPrep_GetFileOffsetInfo(FileOffsetInfo* foi);
extern void CPrep_GetFileOffsetInfo2(FileOffsetInfo* foi, SInt32* pLine, HashNameNode** pName);
extern void CPrep_ResetFileInfo(CPrepFileInfo* file);
extern void CPrep_GetPrepPos(CPrepFileInfo** file, SInt32* ppos);
extern UInt8 C_Compiler(CompilerLinkerParamBlk* cu);
extern void poptokenseq(void);
extern void foundnl(void);
extern void newline(void);
extern SInt16 notendofline(void);
extern char* CPrep_GetFileName(char* buffer, Boolean flag1, Boolean flag2);
extern Boolean macrotest(void);
extern SInt16 CPrep_PragmaLex(Boolean no_err);
extern void CPrep_PushOption(SInt32 id, char value);
extern void CPrep_PopOption(SInt32 id);
extern void preprocessor(void);
extern SInt32 CPrep_BrowserTokenOffset(FileOffsetInfo* foi);
extern SInt32 CPrep_BrowserFileOffset(void);
extern void CPrep_BrowserFilePosition(CPrepFileInfo** fileinfo, SInt32* offset);
extern CPrepFileInfo* CPrep_BrowserCurrentFile(void);
extern void CPrep_GetTokenContext(
    TStreamElement* token,
    CPrepFileInfo** file,
    SInt32* position,
    short* column,
    SInt32* line,
    char* text,
    short* textpos,
    short* textcol,
    char* context,
    short* contextpos
);

#ifdef __cplusplus
}
#endif

#endif
