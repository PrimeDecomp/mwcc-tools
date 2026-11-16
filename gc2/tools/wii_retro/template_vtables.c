/* A pass for Wii 1.0RC1's mwcceppc.exe: after each top-level declaration, emit the vtables of class template
 * instances that have become referenced, and instantiate those instances' referenced members (their virtual
 * functions, which the vtable references). RC1 does both only at the end of the file, so a template instance's
 * virtual destructor comes out after every other function; the Prime games' Wii compiler emits it right after
 * the function that first constructed the class. Other classes' vtables and the members of templates without a
 * vtable (vector::reserve) still wait for the end of the file, as in RC1.
 *
 * retro_GlobalDeclarationActions replaces CParser_ParseDeclaration's call to CMid_GlobalDeclarationActions.
 *
 * RC1 layouts: CClass_Finalize walks two lists of {next 0x0, class 0x4, ...}. TypeClass: vtable 0x1a, flags 0x22
 * (0x800: a template instance). TemplClass: next 0x30, instances 0x42; an instance (a TypeClass) links the next
 * at 0x30 and has is_specialized at 0x41.
 */
typedef unsigned char u8;

extern char* RC1_classlist;  /* 0x756ff4: CClass_Finalize's first list */
extern char* RC1_classlist2; /* 0x751d7c: its second list */
extern char* RC1_templates;  /* 0x751efc */
int RC1_GlobalDeclarationActions(void);    /* 0x47c120 */
int RC1_Finalize(void);                    /* 0x552c20 */
int RC1_GenClassInstance(char* inst, int); /* 0x5c1140 */
void RC1_InstanceFlush(void);              /* 0x47c360 */

#define NEXT(n) (*(char**)(n))
#define CLASS(n) (*(char**)((n) + 4))
#define IS_TEMPL_INST(c) ((*(unsigned*)((c) + 0x22) & 0x800) != 0)
#define VTABLE(c) (*(char**)((c) + 0x1a))
#define MAXNODES 2048

/* Cut *LIST down to its template instances' nodes, saving the nodes in SAVED; returns their count, or -1 when
 * the list is too long to save (then it is left alone). */
static int filter(char** list, char** saved) {
    char *n, **tail;
    int count = 0;
    for (n = *list; n; n = NEXT(n)) {
        if (count == MAXNODES) {
            return -1;
        }
        saved[count++] = n;
    }
    tail = list;
    for (int k = 0; k < count; k++) {
        if (IS_TEMPL_INST(CLASS(saved[k]))) {
            *tail = saved[k];
            tail = (char**)saved[k];
        }
    }
    *tail = 0;
    return count;
}

static void restore(char** list, char** saved, int count) {
    *list = count > 0 ? saved[0] : 0;
    for (int k = 0; k < count; k++) {
        NEXT(saved[k]) = k + 1 < count ? saved[k + 1] : 0;
    }
}

/* CClass_Finalize on the template instances only. */
static int finalize_templates(void) {
    char* saved1[MAXNODES];
    char* saved2[MAXNODES];
    char* head1 = RC1_classlist;
    char* head2 = RC1_classlist2;
    int n1 = filter(&RC1_classlist, saved1);
    int n2 = n1 < 0 ? -1 : filter(&RC1_classlist2, saved2);
    int r = 0;
    if (n1 >= 0 && n2 >= 0) {
        r = RC1_Finalize() & 0xff;
    }
    if (n1 >= 0) {
        restore(&RC1_classlist, saved1, n1);
    }
    if (n2 >= 0) {
        restore(&RC1_classlist2, saved2, n2);
    }
    RC1_classlist = head1;
    RC1_classlist2 = head2;
    return r;
}

/* Instantiate the referenced members of the template instances that have a vtable. */
static int instantiate_polymorphic(void) {
    char *t, *inst;
    int r = 0;
    for (t = RC1_templates; t; t = *(char**)(t + 0x30)) {
        for (inst = *(char**)(t + 0x42); inst; inst = *(char**)(inst + 0x30)) {
            if (IS_TEMPL_INST(inst) && !*(u8*)(inst + 0x41) && VTABLE(inst)) {
                if (RC1_GenClassInstance(inst, 0) & 0xff) {
                    r = 1;
                }
                RC1_InstanceFlush();
            }
        }
    }
    return r;
}

int retro_GlobalDeclarationActions(void) {
    int r = RC1_GlobalDeclarationActions();
    while (finalize_templates() | instantiate_polymorphic()) {
        RC1_GlobalDeclarationActions();
    }
    return r;
}
