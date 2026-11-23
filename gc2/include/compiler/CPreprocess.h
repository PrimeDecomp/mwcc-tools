#ifndef COMPILER_CPREPROCESS_H
#define COMPILER_CPREPROCESS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CPrep_PreprocessDumpNewLine(void);
extern void CPrep_PreprocessDumpFileInfo(Boolean newLine);
#if VERSION == VERSION_WII_1_0RC1
extern void CPrep_PreprocessSetup(void);
extern void CPrep_PreprocessCleanup(void);
extern void CPrep_PreprocessEnterPrefix(void);
extern void CPrep_PreprocessKeepSpace(const char* text, SInt32 size);
extern void CPrep_PreprocessDiscardSpace(void);
extern void CPrep_PreprocessDumpComment(const char* start, const char* end, SInt32 line);
extern Boolean CPrep_PreprocessRecordsDirective(const char* name);
extern void CPrep_PreprocessDumpDirective(const char* text, const char* name);
extern void CPrep_PreprocessDumpPCH(const char* name, Boolean isSystem);
#else
extern void CPrep_PreprocessDumpDirective(char* directive, char* name);
extern void CPrep_PreprocessDumpDirectiveEnd(void);
#endif
extern void CPrep_Preprocess(void);

#ifdef __cplusplus
}
#endif

#endif
