#pragma bool off
#include "version.h"
#if VERSION >= VERSION_GC_1_3

#include "compiler/common.h"
#include "driver/CWParserPluginsPrivate.h"
#include "driver/CLPluginRequests.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/Files.h"

typedef int(__stdcall *ParserCallback1)(CWPluginPrivateContext *context);
typedef int(__stdcall *ParserCallback4)(CWPluginPrivateContext *context, int argument2, int argument3, char *text);

extern "C" {

int __stdcall CWParserPluginsPrivate_GetPanelExtra(void *context, UInt32 *value)
{
    CWPluginPrivateContext *record;

    record = validate_parser_context((CWPluginPrivateContext *)context);
    if (record == NULL) {
        return 4;
    }
    if (value == NULL) {
        return 3;
    }
    *value = record->contextData.parser.f_panelExtra;
    return 0;
}

int __stdcall CWParserPluginsPrivate_SetParserEntry(void *ctx, int index, IntegerSequenceResult *value)
{
    CWPluginPrivateContext *collection;

    collection = validate_parser_context((CWPluginPrivateContext *)ctx);
    if (collection == NULL) {
        return 4;
    }
    if (index < 0 || index >= collection->contextData.parser.count) {
        return 3;
    }
    if (value == NULL) {
        return 3;
    }
    collection->contextData.parser.entries[index] = *value;
    return 0;
}

int __stdcall CWParserPluginsPrivate_CallParserCallback1(void *context)
{
    CWPluginPrivateContext *object;

    object = validate_parser_context((CWPluginPrivateContext *)context);
    if (object == NULL) {
        return 4;
    }
    return ((ParserCallback1)object->contextData.parser.parserCallbacks[1])(object);
}

int __stdcall CWParserPluginsPrivate_CallParserCallback3(void *context, int argument2, int argument3, char *text)
{
    CWPluginPrivateContext *object;

    object = validate_parser_context((CWPluginPrivateContext *)context);
    if (object == NULL) {
        return 4;
    }
    if (text == NULL) {
        return 3;
    }
    return ((ParserCallback4)object->contextData.parser.parserCallbacks[3])(object, argument2, argument3, text);
}

}
#endif
