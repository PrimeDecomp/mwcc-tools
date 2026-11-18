/* Inserted by function_instantiation.py after the recovered ABI declarations.
 * The driver substitutes the implementation and native/source worker tokens.
 * Each callback records its phase before running native code or fixture logic.
 */
extern int RC1_trace_printf(const char*, ...);
extern const char* RC1_trace_link_name(const Object*);
extern unsigned char native_function(void*, void*, unsigned char);

static unsigned char fixture_mode, fixture_ok, fixture_needs, fixture_result;
static unsigned fixture_selected, fixture_replace, fixture_drop_point, fixture_has_point;
static unsigned fixture_noinline, fixture_has_file, fixture_browse_mutation;
static short fixture_token;
static FunctionTemplate fixture_templates[3];
static Object fixture_objects[6], fixture_refs[3];
static FunctionType fixture_types[6];
static NameSpace fixture_spaces[6], fixture_namespace;
static FunctionInstance fixture_instance;
static int fixture_tags[32];
static char fixture_events[64];
static unsigned fixture_event_count;

static const char* trace_name(const Object* object) {
    if (!object) return "<none>";
    if (fixture_mode) return "<fixture>";
    const char* name = *(const char* const*)((const unsigned char*)object+0xc);
    return name ? RC1_trace_link_name(object)+10 : "<unnamed>";
}

static void event(char code) {
    if (fixture_mode) {
        fixture_ok &= fixture_event_count < sizeof(fixture_events);
        if (fixture_event_count < sizeof(fixture_events))
            fixture_events[fixture_event_count++] = code;
    }
    RC1_trace_printf("TF event %c\n", code);
}

static unsigned char zeros(const void* data, unsigned begin, unsigned end) {
    const unsigned char* bytes = data;
    for (unsigned i = begin; i < end; ++i)
        if (bytes[i]) return 0;
    return 1;
}

FunctionTemplate* trace_fallback(const Object* object) {
    event('F');
    if (!fixture_mode) return CTemplTool_Template2TemplFunc(object);
    for (unsigned i = 0; i < 2; ++i)
        if (object == &fixture_refs[i]) return &fixture_templates[i+1];
    fixture_ok = 0;
    return &fixture_templates[2];
}

unsigned char trace_needs(Object* object) {
    event('N');
    RC1_trace_printf("TF needs %s %x %x\n",trace_name(object),object->qualifiers,object->flags);
    if (!fixture_mode) return CIMan_NeedsTemplateInstance(object);
    fixture_ok &= object == &fixture_objects[0];
    if (fixture_replace) fixture_instance.object = &fixture_objects[1];
    return fixture_needs;
}

unsigned char trace_packed(PackedInstanceContext* context, Object* object, void* point,
                            const void* definition, unsigned char explicit_instantiation) {
    event('P');
    RC1_trace_printf("TF packed %s %u %u %x %x\n",trace_name(object),explicit_instantiation,
                    !!point,object->qualifiers,object->flags);
    if (!fixture_mode)
        return RC1_InstantiateStoredDefinition(context,object,point,definition,explicit_instantiation);
    fixture_ok &= context->instance == &fixture_instance && zeros(context,4,0x26) &&
                  object == fixture_instance.object && point == fixture_instance.point_of_instantiation &&
                  definition == fixture_templates[fixture_selected].packed_definition;
    return fixture_result;
}

void trace_error_source(const SourceLocation* location) {
    event('S');
    if (fixture_mode) fixture_ok &= location == &fixture_templates[fixture_selected].location;
    else CError_SetErrorSourceRef(location);
}

void trace_error(int error, Object* object) {
    event('E');
    RC1_trace_printf("TF error %x %s\n",error,trace_name(object));
    if (fixture_mode) fixture_ok &= error == 0x27f9 && object == fixture_instance.object;
    else CError_Error(error,object);
}

void trace_insert(TokenStream* stream, int* state) {
    event('I');
    if (fixture_mode) {
        fixture_ok &= fixture_instance.is_instantiated == 1 &&
                      stream == &fixture_templates[fixture_selected].stream;
        *state = 0x13579bdf;
        RC1_filesyminfo = 5;
    } else CPrep_StreamInsert(stream,state);
}

void trace_decl_source(const SourceLocation* location) {
    event('D');
    if (fixture_mode) {
        fixture_ok &= location == &fixture_templates[fixture_selected].location;
        RC1_filesyminfo = 7;
        cparser_fileoffset.file = fixture_has_file ? &fixture_tags[0] : 0;
    } else CParser_GetDeclSourceInfo(location);
}

short trace_lex(void) {
    event('L');
    RC1_trace_printf("TF lex-debug %u\n", RC1_filesyminfo);
    if (!fixture_mode) return lex();
    fixture_ok &= RC1_filesyminfo == (fixture_noinline || !fixture_has_file ? 0 : 7);
    return fixture_token;
}

void trace_internal(const char* file, int line) {
    event('X');
    RC1_trace_printf("TF internal %d\n",line);
    if (fixture_mode) fixture_ok &= file == RC1_CTemplateFuncInst_source_file && line == 1002;
    else CError_Internal(file,line);
}

const void* trace_browser(void) {
    event('B');
    if (!fixture_mode) return CPrep_BrowserCurrentFile();
    fixture_ok &= tk == fixture_token;
    if (fixture_browse_mutation) {
        fixture_templates[fixture_selected].start_offset = 117;
        fixture_templates[fixture_selected].source_file = &fixture_tags[9];
    }
    return &fixture_tags[1];
}

void* trace_push(const void* klass, Object* object) {
    event('U');
    RC1_trace_printf("TF push %s %u\n",trace_name(object),!!klass);
    if (!fixture_mode) return CTemplTool_PushInstance(klass,object);
    fixture_ok &= !klass && object == fixture_instance.object &&
                  ctempl_curinstance == (fixture_has_point ? (void*)&fixture_tags[3] : (void*)&fixture_tags[2]);
    if (fixture_replace) fixture_instance.object = &fixture_objects[2];
    return &fixture_tags[7];
}

void trace_merge(FunctionType* source, FunctionType* target) {
    event('M');
    if (!fixture_mode) CTemplTool_MergeArgNames(source,target);
    else {
        fixture_ok &= source == fixture_templates[fixture_selected].function->type &&
                      target == fixture_instance.object->type;
        fixture_instance.arguments = &fixture_tags[5];
        fixture_templates[fixture_selected].parameters = &fixture_tags[6];
    }
}

NameSpace* trace_arguments(const void* parameters, const void* arguments, unsigned char flag) {
    event('A');
    if (!fixture_mode) return CTemplTool_SetupTemplateArgumentNameSpace(parameters,arguments,flag);
    fixture_ok &= parameters == &fixture_tags[6] && arguments == &fixture_tags[5] && !flag;
    if (fixture_replace) fixture_instance.object = &fixture_objects[3];
    return &fixture_namespace;
}

void trace_outer(NameSpace* scope) {
    event('O');
    if (!fixture_mode) CTemplTool_SetupOuterTemplateArgumentNameSpace(scope);
    else {
        fixture_ok &= scope == &fixture_namespace && fixture_instance.object->scope == scope &&
                      scope->parent == &fixture_spaces[0];
        if (fixture_replace) {
            fixture_instance.object = &fixture_objects[4];
            fixture_instance.object->scope = scope;
        }
    }
}

void trace_parse(Object* object, DeclInfo* declaration, const void* arg, unsigned char flag1,
                  unsigned char flag2, NameSpace* scope) {
    event('C');
    RC1_trace_printf("TF parse %s %x %x %u\n",trace_name(object),object->qualifiers,object->flags,RC1_filesyminfo);
    if (!fixture_mode) CFunc_ParseFuncDef(object,declaration,arg,flag1,flag2,scope);
    else {
        fixture_ok &= object == fixture_instance.object && !arg && !flag1 && !flag2 &&
                      scope == &fixture_namespace && object->scope == scope &&
                      declaration->file == &fixture_tags[1] && declaration->source_file == &fixture_tags[8] &&
                      declaration->source_offset == (fixture_browse_mutation ? 117 : 17) &&
                      zeros(declaration,0,0x90) && zeros(declaration,0x9c,0xa4);
        declaration->file = &fixture_tags[10];
        declaration->source_file = &fixture_tags[11];
        declaration->source_offset = 119;
        fixture_templates[fixture_selected].end_offset = 120;
        scope->parent = &fixture_spaces[1];
        ctempl_curinstance = &fixture_tags[12];
    }
}

void trace_remove_outer(NameSpace* scope) {
    event('R');
    if (!fixture_mode) CTemplTool_RemoveOuterTemplateArgumentNameSpace(scope);
    else {
        fixture_ok &= scope == &fixture_namespace && fixture_instance.object->scope == scope;
        if (fixture_replace) fixture_instance.object = &fixture_objects[1];
    }
}

void trace_pop(void* stack) {
    event('T');
    if (!fixture_mode) CTemplTool_PopInstance(stack);
    else {
        fixture_ok &= stack == &fixture_tags[7] && fixture_instance.object->scope == &fixture_spaces[1];
        if (fixture_drop_point) fixture_instance.point_of_instantiation = 0;
    }
}

void trace_remove(TokenStream* stream, int* state) {
    event('V');
    if (!fixture_mode) CPrep_StreamRemove(stream,state);
    else {
        fixture_ok &= stream == &fixture_templates[fixture_selected].stream && *state == 0x13579bdf &&
                      ctempl_curinstance == (fixture_has_point && !fixture_drop_point ?
                                            (void*)&fixture_tags[2] : (void*)&fixture_tags[12]);
        RC1_filesyminfo = 33;
    }
}

void trace_browse(Object* object, const void* file, const void* source, int start, int end) {
    event('W');
    RC1_trace_printf("TF browse %s %d %d %u\n",trace_name(object),start,end,RC1_filesyminfo);
    if (!fixture_mode) CBrowse_NewFunction(object,file,source,start,end);
    else fixture_ok &= object == fixture_instance.object && file == &fixture_tags[10] &&
                       source == &fixture_tags[11] && start == 119 && end == 120 && RC1_filesyminfo == 7;
}

IMPLEMENTATION

static void fixture_reset(void) {
    memclrw(fixture_templates,sizeof(fixture_templates));
    memclrw(fixture_objects,sizeof(fixture_objects));
    memclrw(fixture_refs,sizeof(fixture_refs));
    memclrw(fixture_types,sizeof(fixture_types));
    memclrw(fixture_spaces,sizeof(fixture_spaces));
    memclrw(&fixture_namespace,sizeof(fixture_namespace));
    memclrw(&fixture_instance,sizeof(fixture_instance));
    memclrw(fixture_events,sizeof(fixture_events));
    fixture_ok = fixture_needs = fixture_result = fixture_has_file = 1;
    fixture_selected = fixture_replace = fixture_drop_point = fixture_has_point = 0;
    fixture_noinline = fixture_browse_mutation = fixture_event_count = 0;
    fixture_token = '{';
    for (unsigned i = 0; i < 6; ++i) {
        fixture_objects[i].type = &fixture_types[i];
        fixture_objects[i].scope = &fixture_spaces[0];
        fixture_objects[i].qualifiers = 0x80;
        fixture_objects[i].flags = 0x40;
        fixture_objects[i].storage_class = 0x101;
        fixture_types[i].kind = FunctionTypeKind;
    }
    for (unsigned i = 0; i < 3; ++i) {
        fixture_templates[i].function = &fixture_objects[5];
        fixture_templates[i].source_file = &fixture_tags[8];
        fixture_templates[i].start_offset = 17;
        fixture_templates[i].end_offset = 18;
    }
    fixture_instance.object = &fixture_objects[0];
    fixture_instance.arguments = &fixture_tags[4];
    ctempl_curinstance = &fixture_tags[2];
    RC1_nosyminline = 0;
    RC1_filesyminfo = 3;
}

static void fixture_check(const char* label, unsigned char explicit_instantiation,
                           unsigned expected, unsigned instantiated, const char* events) {
    RC1_nosyminline = fixture_noinline;
    fixture_instance.point_of_instantiation = fixture_has_point ? &fixture_tags[3] : 0;
    unsigned char result = FUNCTION_WORKER(&fixture_templates[0],&fixture_instance,explicit_instantiation);
    unsigned n = 0;
    while (events[n]) {
        fixture_ok &= n < fixture_event_count && fixture_events[n] == events[n];
        ++n;
    }
    fixture_ok &= n == fixture_event_count && result == expected &&
                  fixture_instance.is_instantiated == instantiated;
    RC1_trace_printf("TF fixture-%s %u\n",label,fixture_ok);
}

static void function_fixtures(void) {
    unsigned char saved_nosyminline = RC1_nosyminline, saved_debug = RC1_filesyminfo;
    SourceLocation saved_location = cparser_fileoffset;
    void* saved_instance = ctempl_curinstance;
    short saved_tk = tk;
    fixture_mode = 1;
    fixture_reset();
    fixture_instance.is_extern = 1;
    fixture_check("extern-skip",0,0,0,"");
    for (unsigned static_storage = 0; static_storage < 2; ++static_storage) {
        fixture_reset();
        fixture_needs = 0;
        fixture_replace = 1;
        fixture_objects[1].storage_class = static_storage ? StaticStorageClass : 0x101;
        fixture_check("declined",0,0,1,"N");
        RC1_trace_printf("TF fixture-declined-flags %u\n",fixture_objects[0].qualifiers == 0x80 &&
                         fixture_objects[1].qualifiers == 0x20080 && fixture_objects[1].flags == 0x40);
    }
    for (unsigned storage = 0; storage < 2; ++storage)
        for (unsigned explicit_value = 0; explicit_value < 3; ++explicit_value) {
            unsigned char explicit_instantiation = explicit_value == 2 ? 255 : explicit_value;
            fixture_reset();
            fixture_objects[0].storage_class = storage ? StaticStorageClass : 0x101;
            fixture_check("missing",explicit_instantiation,0,0,explicit_value ? "SE" : "N");
            RC1_trace_printf("TF fixture-missing-flags %u\n",fixture_objects[0].qualifiers ==
                (storage ? 0x80u : 0x20080u) && fixture_objects[0].flags ==
                (!storage && explicit_value ? 0x48u : 0x40u));
        }
    for (unsigned result = 0; result < 3; ++result) {
        fixture_reset();
        fixture_instance.is_extern = 1;
        fixture_templates[0].packed_definition = &fixture_tags[20];
        fixture_templates[0].stream.tokens = &fixture_tags[21];
        fixture_has_point = 1;
        fixture_result = result == 2 ? 255 : result;
        fixture_check("packed-result",255,fixture_result,0,"P");
    }
    fixture_reset();
    for (unsigned i = 0; i < 2; ++i) {
        fixture_templates[i].fallback_object = &fixture_refs[i];
        fixture_templates[i].fallback_context = &fixture_tags[22+i];
    }
    fixture_templates[2].packed_definition = &fixture_tags[24];
    fixture_selected = 2;
    fixture_replace = 1;
    fixture_needs = 255;
    fixture_check("fallback-chain",0,1,0,"FFNP");
    RC1_trace_printf("TF fixture-fallback-flags %u\n",fixture_objects[0].qualifiers == 0x80 &&
                     fixture_objects[1].qualifiers == 0x20080 && fixture_objects[1].flags == 0x40);
    for (unsigned field = 0; field < 2; ++field) {
        fixture_reset();
        fixture_templates[0].fallback_object = field ? &fixture_refs[0] : 0;
        fixture_templates[0].fallback_context = field ? 0 : &fixture_tags[22];
        fixture_check("fallback-stop",1,0,0,"SE");
    }
    for (unsigned flags = 0; flags < 16; ++flags) {
        fixture_reset();
        fixture_templates[0].stream.tokens = &fixture_tags[21];
        fixture_templates[0].fallback_object = &fixture_refs[0];
        fixture_templates[0].fallback_context = &fixture_tags[22];
        fixture_has_point = !!(flags & 1);
        fixture_noinline = !!(flags & 2);
        fixture_has_file = !!(flags & 4);
        fixture_replace = fixture_browse_mutation = !!(flags & 8);
        fixture_drop_point = fixture_replace && fixture_has_point;
        fixture_token = flags % 4 == 0 ? '{' : flags % 4 == 1 ? ':' : flags % 4 == 2 ? 0x15e : 'x';
        fixture_check("stream",flags & 1,1,1,
            fixture_token == 'x' ? (flags & 1 ? "IDLXBUMAOCRTVW" : "NIDLXBUMAOCRTVW") :
                                  (flags & 1 ? "IDLBUMAOCRTVW" : "NIDLBUMAOCRTVW"));
    }
    fixture_mode = 0;
    RC1_nosyminline = saved_nosyminline;
    RC1_filesyminfo = saved_debug;
    cparser_fileoffset = saved_location;
    ctempl_curinstance = saved_instance;
    tk = saved_tk;
}

unsigned char retro_GenerateFunctionInstance(void* templ, void* instance, unsigned char explicit_instantiation) {
    static unsigned char tested;
    if (!tested) { tested = 1; function_fixtures(); }
    FunctionInstance* entry = instance;
    RC1_trace_printf("TF enter %s %u %u %u %u %x %x\n",trace_name(entry->object),explicit_instantiation,
                    entry->is_instantiated,entry->is_specialized,entry->is_extern,
                    entry->object->qualifiers,entry->object->flags);
    unsigned char result = FUNCTION_WORKER(templ,instance,explicit_instantiation);
    RC1_trace_printf("TF leave %s %u %u %x %x\n",trace_name(entry->object),result,entry->is_instantiated,
                    entry->object->qualifiers,entry->object->flags);
    return result;
}
