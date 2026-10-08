#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include "symbol.h"

// sh_type
// This member categorizes the section's contents and
// semantics. perhaps use this instead of string comparison

// st_info
//               This member specifies the symbol's type and binding
//               attributes:

//               STT_NOTYPE
//                      The symbol's type is not defined.

//               STT_OBJECT
//                      The symbol is associated with a data object.

//               STT_FUNC
//                      The symbol is associated with a function or other
//                      executable code.

//               STT_SECTION
//                      The symbol is associated with a section.  Symbol
//                      table entries of this type exist primarily for
//                      relocation and normally have STB_LOCAL bindings.

//               STT_FILE
//                      By convention, the symbol's name gives the name of
//                      the source file associated with the object file.  A
//                      file symbol has STB_LOCAL bindings, its section
//                      index is SHN_ABS, and it precedes the other
//                      STB_LOCAL symbols of the file, if it is present.

//               STB_LOCAL
//                      Local symbols are not visible outside the object
//                      file containing their definition.  Local symbols of
//                      the same name may exist in multiple files without
//                      interfering with each other.

//               STB_GLOBAL
//                      Global symbols are visible to all object files being
//                      combined.  One file's definition of a global symbol
//                      will satisfy another file's undefined reference to
//                      the same symbol.

//               STB_WEAK
//                      Weak symbols resemble global symbols, but their
//                      definitions have lower precedence.

static const char *bind_tab[] = {   //look up tables for binding type
    [STB_LOCAL]  = "LOCAL",
    [STB_GLOBAL] = "GLOBAL",
    [STB_WEAK]   = "WEAK"
};

static const char *type_tab[] = {
    [STT_NOTYPE]  = "NOTYPE",
    [STT_OBJECT]  = "OBJECT",
    [STT_FUNC]    = "FUNC",
    [STT_SECTION] = "SECTION",
    [STT_FILE]    = "FILE",
    [STT_COMMON]  = "COMMON"
};

static const char *shndx_tab[] = {
    [SHN_UNDEF]  = "UNDEF",
    [SHN_ABS]    = "ABS",
    [SHN_COMMON] = "COMMON"
};

static const char *tab_lookup(const char *tab[], size_t tab_n, unsigned idx, const char *fallback){ //map enum value to stirng
    if (idx < tab_n && tab[idx]) {
        return tab[idx];
    }
    return fallback;
}

static const char *bind_name(unsigned b){   //converting into string for debug
    return tab_lookup(bind_tab, sizeof(bind_tab)/sizeof(bind_tab[0]), b, "OTHER");
}

static const char *type_name(unsigned t){
    return tab_lookup(type_tab, sizeof(type_tab)/sizeof(type_tab[0]), t, "OTHER");
}

static int is_external(const Elf32_Sym *s){     //global resolution
    unsigned b = ELF32_ST_BIND(s->st_info);
    unsigned t = ELF32_ST_TYPE(s->st_info);
    if (b == STB_LOCAL) {
        return 0;
    }
    if (t == STT_FILE || t == STT_SECTION){
        return 0;
    }
    return 1;
}

static int g_find(global_stable *g, const char *name){  //check global table arrary for existing name
    for (size_t i = 0; i < g->n; i++)
        if (strcmp(g->v[i].name, name) == 0) 
            return (int)i;
    return -1;
}

static void g_push(global_stable *g, g_symbol s){   //push new name to global table array
    if (g->n == g->cap) {
        size_t nc = g->cap ? g->cap * 2 : 64;
        g_symbol *nv = realloc(g->v, nc * sizeof(*nv));
        if (!nv) { perror("realloc"); 
            exit(1); 
        }
        g->v = nv; g->cap = nc;
    }
    g->v[g->n++] = s;
}

global_stable *gs_new(void){        // build empty table
    global_stable *g = calloc(1, sizeof(*g));
    if (!g) { 
        perror("calloc"); 
        exit(1); 
    }
    return g;
}

void gs_free(global_stable *g){ // free the table
    if (!g) {
        return;
    }
    for (size_t i = 0; i < g->n; i++) {
        free(g->v[i].name);
        free(g->v[i].from);
    }
    free(g->v);
    free(g);
}

static int resolve_insert(global_stable *g, const char *path, const char *name, const Elf32_Sym *s){    //resolve depending on bind and definition
    unsigned bind = ELF32_ST_BIND(s->st_info);
    unsigned type = ELF32_ST_TYPE(s->st_info);
    int def = (s->st_shndx != SHN_UNDEF);
    int weak = (bind == STB_WEAK);

    int idx = g_find(g, name);
    if (idx < 0) {
        g_symbol gs = {
            .name = strdup(name),
            .from = strdup(path),
            .bind = bind,
            .type = type,
            .shndx = s->st_shndx,
            .value = s->st_value,
            .size = s->st_size,
            .is_defined = def,
            .is_weak = weak
        };
        if (!gs.name || !gs.from) { perror("strdup"); exit(1); }
        g_push(g, gs);
        return 0;
    }

    g_symbol *cur = &g->v[idx];

    // if new is undefined, it never improves the table
    if (!def) return 0;

    // if current is undefined, take the definition
    if (!cur->is_defined) {
        cur->from = (free(cur->from), strdup(path));
        cur->bind = bind; cur->type = type; cur->shndx = s->st_shndx;
        cur->value = s->st_value; cur->size = s->st_size;
        cur->is_defined = 1; cur->is_weak = weak;
        return 0;
    }

    // both defined
    int cur_strong = (!cur->is_weak);
    int new_strong = (!weak);

    if (cur_strong && new_strong) {
        fprintf(stderr, "ERROR: multiple strong definitions of %s (%s and %s)\n", name, cur->from, path);
        return -1;
    }

    // strong overrides weak
    if (new_strong && !cur_strong) {
        cur->from = (free(cur->from), strdup(path));
        cur->bind = bind; cur->type = type; cur->shndx = s->st_shndx;
        cur->value = s->st_value; cur->size = s->st_size;
        cur->is_defined = 1; cur->is_weak = weak;
    }

    return 0;
}


static const char *shndx_name(uint16_t s){  //debug

    if (s == SHN_UNDEF)  return "UNDEF";
    if (s == SHN_ABS)    return "ABS";
    if (s == SHN_COMMON) return "COMMON";
    return "SECT";
}

void symbol(FILE *f, const Elf32_Ehdr *ehdr, const Elf32_Shdr *sh_table, const char *sh_strs){  //old
    int symtab_i = -1;

    // find symtab section
    for (int i = 0; i < (int)ehdr->e_shnum; i++) {
        const char *name = &sh_strs[sh_table[i].sh_name];
        if (strcmp(name, ".symtab") == 0) { symtab_i = i; break; }
    }

    if (symtab_i < 0) {
        printf("No symbol table\n");
        return;
    }

    Elf32_Shdr symtab = sh_table[symtab_i];
    Elf32_Shdr strtab = sh_table[symtab.sh_link];

    // read strtab
    char *sym_strs = malloc((size_t)strtab.sh_size);
    if (!sym_strs) { 
        perror("malloc sym_strs"); 
        return; 
    }
    if (fseek(f, (long)strtab.sh_offset, SEEK_SET) != 0) {
        perror("fseek strtab"); 
        free(sym_strs);
        return; 
    }
    if (fread(sym_strs, 1, (size_t)strtab.sh_size, f) != (size_t)strtab.sh_size) {
        fprintf(stderr, "Failed to read .strtab\n");
        free(sym_strs);
        return;
    }

    // read symbols
    int nsyms = (int)(symtab.sh_size / sizeof(Elf32_Sym));
    Elf32_Sym *syms = malloc((size_t)nsyms * sizeof(Elf32_Sym));
    if (!syms) { 
        perror("malloc syms"); 
        free(sym_strs); 
        return; 
    }
    if (fseek(f, (long)symtab.sh_offset, SEEK_SET) != 0) { 
        perror("fseek symtab"); 
        free(syms); 
        free(sym_strs); 
        return; 
    }
    if (fread(syms, sizeof(Elf32_Sym), (size_t)nsyms, f) != (size_t)nsyms) {
        fprintf(stderr, "Failed to read .symtab\n");
        free(syms);
        free(sym_strs);
        return;
    }

    printf("Symbol Table:\n");
    printf("  [Nr] %-20s %-6s %-7s %-10s %-18s %-6s\n", "Name", "Bind", "Type", "Shndx", "Value", "Size");

    int def = 0, undef = 0;

    for (int i = 0; i < nsyms; i++) {
        const char *nm = "<badname>";
        if (syms[i].st_name < strtab.sh_size) nm = &sym_strs[syms[i].st_name];
        unsigned bind = ELF32_ST_BIND(syms[i].st_info);
        unsigned type = ELF32_ST_TYPE(syms[i].st_info);

        if (syms[i].st_shndx == SHN_UNDEF) undef++; else def++;

        printf("  [%2d] %-20s %-6s %-7s %-6s(%4u) 0x%08lx %-6lu\n",
               i,
               nm,
               bind_name(bind),
               type_name(type),
               shndx_name(syms[i].st_shndx),
               (unsigned)syms[i].st_shndx,
               (unsigned long)syms[i].st_value,
               (unsigned long)syms[i].st_size);
    }

    printf("Summary: %d defined, %d undefined\n", def, undef);

    printf("Undefined externals:\n");
    for (int i = 0; i < nsyms; i++) {
        unsigned bind = ELF32_ST_BIND(syms[i].st_info);
        if (syms[i].st_shndx == SHN_UNDEF && (bind == STB_GLOBAL || bind == STB_WEAK)) {
            printf("  %s\n", &sym_strs[syms[i].st_name]);
        }
    }

    free(syms);
    free(sym_strs);
}

//pass 1 for symbol collection
int gs_add_object(global_stable *g, const char *path, FILE *f, const Elf32_Ehdr *ehdr, const Elf32_Shdr *sh_table, const char *sh_strs){
    int symtab_i = -1;
    for (int i = 0; i < (int)ehdr->e_shnum; i++) {
        const char *nm = &sh_strs[sh_table[i].sh_name];
        if (strcmp(nm, ".symtab") == 0) { 
            symtab_i = i;
            break;
        }
    }
    if (symtab_i < 0){
        return 0;
    }

    Elf32_Shdr symtab = sh_table[symtab_i];         //load symbol table
    Elf32_Shdr strtab = sh_table[symtab.sh_link];   //load table entries

    char *sym_strs2 = malloc((size_t)strtab.sh_size);
    if (!sym_strs2) {
        perror("malloc");
        return -1;
    }
    if (fseek(f, (long)strtab.sh_offset, SEEK_SET) != 0) {
        perror("fseek");
        free(sym_strs2);
        return -1;
    }
    if (fread(sym_strs2, 1, (size_t)strtab.sh_size, f) != (size_t)strtab.sh_size) {
        fprintf(stderr, "Failed to read .strtab in %s\n", path);
        free(sym_strs2);
        return -1;
    }

    int nsyms = (int)(symtab.sh_size / sizeof(Elf32_Sym));
    Elf32_Sym *syms = malloc((size_t)nsyms * sizeof(*syms));
    if (!syms) { 
        perror("malloc"); 
        free(sym_strs2); 
        return -1; 
    }
    if (fseek(f, (long)symtab.sh_offset, SEEK_SET) != 0) { 
        perror("fseek"); 
        free(syms); 
        free(sym_strs2); 
        return -1; 
    }
    if (fread(syms, sizeof(*syms), (size_t)nsyms, f) != (size_t)nsyms) {
        fprintf(stderr, "Failed to read .symtab in %s\n", path);
        free(syms); 
        free(sym_strs2);
        return -1;
    }

    for (int i = 0; i < nsyms; i++) {
        if (!is_external(&syms[i])) {
            continue;
        }
        if (syms[i].st_name >= strtab.sh_size) {
            continue;
        }
        const char *name = &sym_strs2[syms[i].st_name];
        if (!*name) {
            continue;
        }
        if (resolve_insert(g, path, name, &syms[i]) != 0) {
            free(syms); 
            free(sym_strs2);
            return -1;
        }
    }

    free(syms);
    free(sym_strs2);
    return 0;
}

int gs_finish(global_stable *g){    // print global table after pass 1
    int errors = 0;

    printf("\nGLOBAL SYMBOL TABLE:\n");
    printf("%-24s %-8s %-7s %-10s %-18s %-6s %s\n",
           "Name", "State", "Bind", "Type", "Value", "Size", "From");

    for (size_t i = 0; i < g->n; i++) {
        const g_symbol *s = &g->v[i];
        const char *state = s->is_defined ? "DEF" : "UNDEF";
        printf("%-24s %-8s %-7s %-7s 0x%08lx %-6lu %s\n", s->name, state, bind_name(s->bind), type_name(s->type), (unsigned)s->value, (unsigned)s->size, s->from);

        if (!s->is_defined) {
            errors++;
        }
    }

    if (errors) {
        printf("\nUNRESOLVED UNDEFINED SYMBOLS: %d\n", errors);
        return -1;
    }
    printf("\nAll symbols resolved\n");
    return 0;
}


const g_symbol *gs_lookup(const global_stable *g, const char *name) {
    if (!g || !name) {
        return NULL;
    }
    for (size_t i = 0; i < g->n; i++) {
        if (strcmp(g->v[i].name, name) == 0) {
            return &g->v[i];
        }
    }
    return NULL;
}

