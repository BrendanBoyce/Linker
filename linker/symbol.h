#ifndef SYMBOL_H
#define SYMBOL_H

#include <stdint.h>
#include <stdio.h>
#include <elf.h>

typedef struct global_symbols global_stable;

typedef struct {
    const char *name;
    const char *from;
    uint16_t shndx;
    uint32_t value;
    uint32_t size;
    int is_defined;
    int is_weak;
} sym_view;

typedef struct {
    char *name;
    char *from;
    unsigned bind;
    unsigned type;
    uint16_t shndx;
    uint32_t value;
    uint32_t size;
    int is_defined;
    int is_weak;
} g_symbol;

typedef struct global_symbols {
    g_symbol *v;
    size_t n, cap;
} global_stable;

global_stable *gs_new(void);

const sym_view *gs_lookup_view(const global_stable *g, const char *name);
const g_symbol *gs_lookup(const global_stable *g, const char *name);

void gs_free(global_stable *g);
int gs_add_object(global_stable *g, const char *path, FILE *f, const Elf32_Ehdr *ehdr, const Elf32_Shdr *sh_table, const char *sh_strs);
int gs_finish(global_stable *g);
#endif
