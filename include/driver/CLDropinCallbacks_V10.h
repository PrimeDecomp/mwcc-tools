#ifndef DRIVER_CLDROPINCALLBACKS_V10_H
#define DRIVER_CLDROPINCALLBACKS_V10_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/CWPluginsPrivate.h"

#ifdef __cplusplus
extern "C" {
#endif

// (Pro7's plugin_internal.h: the shell's context for a plugin)
#pragma pack(push, 2)
typedef struct shellContextType {
    struct Plugin* plugin;
    Boolean userAccessPathsChanged;
    Boolean systemAccessPathsChanged;
} shellContextType;
#pragma pack(pop)

// (Pro7's plugin.h, 2-byte aligned like the SDK's structs)
#pragma pack(push, 2)
struct IDEAccessPathList {
    SInt32 userPathCount;
    struct IDEAccessPath* userPaths;
    SInt32 systemPathCount;
    struct IDEAccessPath* systemPaths;
    Boolean alwaysSearchUserPaths;
    Boolean convertPaths;
};
struct IDEAccessPath {
    CWFileSpec pathSpec;
    Boolean recursive;
    SInt32 subdirectoryCount;
    CWFileSpec* subdirectories;
};
#pragma pack(pop)

// The base table's callbacks other units call (Pro7's plugin.h declares them all)
extern CW_CALLBACK UCBFindAndLoadFile(
    CWPluginContext context, const char* infilename, CWFileInfo* fileinfo
);
extern CW_CALLBACK UCBGetFileText(
    CWPluginContext context,
    const CWFileSpec* filespec,
    const char** text,
    SInt32* textLength,
    short* filedatatype
);
extern CW_CALLBACK UCBReleaseFileText(CWPluginContext context, const char* text);
extern CW_CALLBACK UCBSecretAttachHandle(
    CWPluginContext context, Handle handle, CWMemHandle* memHandle
);
extern CW_CALLBACK UCBSecretDetachHandle(
    CWPluginContext context, CWMemHandle memHandle, Handle* handle
);

#ifdef __cplusplus
}
#endif

#endif
