#ifndef ALLOCATION_H
#define ALLOCATION_H
#include <stdio.h>
#include <stdint.h>
#include <elf.h>

typedef enum {
    OUT_TEXT = 0,
    OUT_RODATA,
    OUT_DATA,
    OUT_BSS,
    OUT_COUNT
} OutBucket;

typedef struct {
    int used; // 0 = section not included
    OutBucket bucket; // which output section bucket
    uint64_t out_off; // offset within that output section
} section_placement;

typedef struct {
    uint8_t *buf[OUT_COUNT];   // null for out_bss
    uint64_t size[OUT_COUNT];  // allocated size
} out_image;

typedef struct {    //moved from allocation.c
    struct {
        const char *name;
        uint64_t align;
        uint64_t memsz;
        uint64_t filesz;
        uint64_t file_off;
        uint64_t vaddr;
        uint64_t flags;
    } out[OUT_COUNT];
    size_t shnum;
    section_placement *place;
} layout_map;

typedef struct {
    size_t nobj;
    size_t *shnum;
    section_placement **place;
    struct {
        const char *name;
        uint64_t align, memsz, filesz, file_off, vaddr, flags;
    } out[OUT_COUNT];
} link_layout;

typedef struct {        //moved from reader.c
    const char *path;
    FILE *f;
    Elf32_Ehdr ehdr;
    Elf32_Shdr *sh_table;
    char *sh_strs;
} input_object;

int layout_build_all(link_layout *L, const input_object *objects, size_t nobj);
void layout_free_all(link_layout *L);
void layout_assign_addresses_all(link_layout *L, uint64_t file_base, uint64_t vaddr_base);
int build_out_image_all(out_image *img, const input_object *objects, size_t nobj, const link_layout *L);

// Free resources in layout_map
void layout_free(layout_map *lm);

void free_out_image(out_image *img);

#endif
