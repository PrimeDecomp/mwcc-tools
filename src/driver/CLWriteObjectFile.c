#include "driver/CLWriteObjectFile.h"
#include "driver/CLBrowser.h"
#include "driver/CLErrors.h"
#include "driver/CLFileOps.h"
#include "driver/CLIO.h"
#include "driver/CLMain.h"
#include "driver/CLPlugins.h"
#include "driver/Generic.h"
#include "driver/MacSpecs.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int WriteObjectFile(File* file, OSType maccreator, OSType mactype) {
    FSSpec srcfss;
    FSSpec outfss;

    OS_ASSERT(22, file->objectdata && file->compiler);

    OS_OSSpec_To_FSSpec(&file->outfss, &outfss);
    OS_OSSpec_To_FSSpec(&file->srcfss, &srcfss);

    if (optsCmdLine.verbose) {
        CLReport(
            CLStr16,
            (file->tempOnDisk & CmdLineStageMask_Cg) ? "temporary " : "",
            OS_SpecToStringRelative(&file->outfss, NULL, STSbuf, sizeof(STSbuf))
        );
    }

    if (!Plugin_CL_WriteObjectFile(
            file->compiler, &srcfss, &outfss, maccreator, mactype, file->objectdata
        )) {
        return 0;
    } else {
        return 1;
    }
}

int WriteBrowseData(File* file, OSType maccreator, OSType mactype) {
    OSHandle browsehandle;
    OSSpec outfss;
    const CWObjectFlags* cof;

    cof = Plugin_CL_GetObjectFlags(file->compiler);
    outfss = file->outfss;

    OS_NameSpecSetExtension(
        &outfss.name, optsCompiler.brsFileExt[0] ? optsCompiler.brsFileExt : cof->brsFileExt
    );
    if (optsCmdLine.verbose) {
        CLReport(CLStr17, OS_SpecToStringRelative(&outfss, NULL, STSbuf, sizeof(STSbuf)));
    }

    if (!Browser_PackBrowseFile(file->browsedata, &clState.browseTableHandle, &browsehandle)) {
        return 0;
    }

    if (!WriteBinaryHandleToFile(&outfss, maccreator, mactype, &browsehandle)) {
        return 0;
    }
    return 1;
}
