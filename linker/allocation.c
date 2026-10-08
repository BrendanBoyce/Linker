#include "allocation.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// further details for linux man:

// sh_type
//               This member categorizes the section's contents and
//               semantics.

// SHT_NOBITS
// A section of this type occupies no space in the file
// but otherwise resembles SHT_PROGBITS.  Although this
// section contains no bytes, the sh_offset member
// contains the conceptual file offset.


static uint64_t alignment(uint64_t x, uint64_t a) { //rounding up
    if (a == 0) {
        return x;
    }
    uint64_t r = x % a;
    return r ? (x + (a - r)) : x;
}


static int starts_with(const char *s, const char *p) {  //section name classification
    while (*p) {
        if (*s++ != *p++) {
            return 0;
        }
    }
    return 1;
}

static int is_file_backed_bucket(int b) {
    return (b == OUT_TEXT || b == OUT_RODATA || b == OUT_DATA); //should bytes be written out
}

static int pick_bucket(const char *secname, const Elf32_Shdr *sh) {     //decide which output bucket input belong to
    if (!(sh->sh_flags & SHF_ALLOC)) {
        return -1;
    }

    if (starts_with(secname, ".text"))   return OUT_TEXT;
    if (starts_with(secname, ".rodata")) return OUT_RODATA;
    if (starts_with(secname, ".data"))   return OUT_DATA;
    if (starts_with(secname, ".bss") || sh->sh_type == SHT_NOBITS) 
        return OUT_BSS;

    return -1;
}


int layout_build_all(link_layout *L, const input_object *objects, size_t nobj) {   //layout pass
    memset(L, 0, sizeof(*L));
    L->nobj = nobj;

    // initialise output buckets
    L->out[OUT_TEXT].name   = ".text";
    L->out[OUT_RODATA].name = ".rodata";
    L->out[OUT_DATA].name   = ".data";
    L->out[OUT_BSS].name    = ".bss";
    for (int b = 0; b < OUT_COUNT; b++) {
        L->out[b].align = 1;
    }

    // allocate per object placement arrays
    L->shnum = calloc(nobj, sizeof(*L->shnum));
    L->place = calloc(nobj, sizeof(*L->place));
    if (!L->shnum || !L->place) {
        return -1;
    }

    for (size_t oi = 0; oi < nobj; oi++) {
        size_t shnum = objects[oi].ehdr.e_shnum;
        L->shnum[oi] = shnum;

        L->place[oi] = calloc(shnum, sizeof(section_placement));
        if (!L->place[oi]) {
            return -1;
        }

        const Elf32_Shdr *sh_table = objects[oi].sh_table;
        const char *sh_strs = objects[oi].sh_strs;

        for (size_t si = 0; si < shnum; si++) {
            const Elf32_Shdr *sh = &sh_table[si];
            const char *name = &sh_strs[sh->sh_name];

            int b = pick_bucket(name, sh);  // decide if it will contribute
            if (b < 0) {
                continue;
            }

            uint64_t a = sh->sh_addralign ? sh->sh_addralign : 1;
            if (a > L->out[b].align) L->out[b].align = a;

            uint64_t out_off = alignment(L->out[b].memsz, a);   //assign section offset within bucket

            L->place[oi][si].used = 1;
            L->place[oi][si].bucket = (OutBucket)b;
            L->place[oi][si].out_off = out_off;

            L->out[b].memsz = out_off + sh->sh_size;    //grow size when needed
            L->out[b].flags |= sh->sh_flags;

            if (sh->sh_type != SHT_NOBITS) {
                uint64_t end = out_off + sh->sh_size;
                if (end > L->out[b].filesz) L->out[b].filesz = end;
            }
        }
    }
    return 0;
}

//image pass

int build_out_image_all(out_image *img, const input_object *objects, size_t nobj, const link_layout *L) {
    memset(img, 0, sizeof(*img));

    for (int b = 0; b < OUT_COUNT; b++) {   //allocate output buffers
        if (!is_file_backed_bucket(b)){
            continue;
        }
        uint64_t sz = L->out[b].filesz;
        img->size[b] = sz;
        if (sz == 0) {
            continue;
        }

        img->buf[b] = calloc(1, (size_t)sz);
        if (!img->buf[b]) {
            perror("calloc out_image_all");
            free_out_image(img);
            return -1;
        }
    }

    for (size_t oi = 0; oi < nobj; oi++) {  //copy bytes into output buffers
        FILE *f = objects[oi].f;
        const Elf32_Shdr *sh_table = objects[oi].sh_table;

        for (size_t si = 0; si < L->shnum[oi]; si++) {
            if (!L->place[oi][si].used) {
                continue;
            }

            const Elf32_Shdr *sh = &sh_table[si];
            int b = (int)L->place[oi][si].bucket;

            if (sh->sh_type == SHT_NOBITS){
                continue;
            }

            if (!is_file_backed_bucket(b)) continue;
            if (!img->buf[b]) continue;

            uint64_t dst_off = L->place[oi][si].out_off;
            uint64_t n = sh->sh_size;

            if (dst_off + n > img->size[b]) {
                fprintf(stderr, "%s: copy overflow oi=%zu si=%zu bucket=%d dst=0x%lx n=0x%lx out=0x%lx\n",
                        objects[oi].path, oi, si, b,
                        (unsigned long)dst_off, (unsigned long)n, (unsigned long)img->size[b]);
                free_out_image(img);
                return -1;
            }

            if (fseek(f, (long)sh->sh_offset, SEEK_SET) != 0) {
                perror("fseek section");
                free_out_image(img);
                return -1;
            }
            if (fread(img->buf[b] + dst_off, 1, (size_t)n, f) != (size_t)n) {
                fprintf(stderr, "%s: failed to read section bytes\n", objects[oi].path);
                free_out_image(img);
                return -1;
            }
        }
    }

    return 0;
}


void free_out_image(out_image *img) {   //free buffers
    for (int b = 0; b < OUT_COUNT; b++) {
        free(img->buf[b]);
        img->buf[b] = NULL;
        img->size[b] = 0;
    }
}

//address assignment pass

void layout_assign_addresses(layout_map *lm, uint64_t file_base, uint64_t vaddr_base) {
    uint64_t cur = file_base;
    for (int b = 0; b < OUT_COUNT; b++) {   //assign file offsets
        if (b == OUT_BSS) continue;
        if (lm->out[b].filesz == 0) continue;
        cur = alignment(cur, lm->out[b].align);
        lm->out[b].file_off = cur;
        cur += lm->out[b].filesz;
    }

    uint64_t v = vaddr_base;    //assign virtual addresses

    for (int b = OUT_TEXT; b <= OUT_RODATA; b++) {
        if (lm->out[b].memsz == 0) continue;
        v = alignment(v, lm->out[b].align);
        lm->out[b].vaddr = v;
        v += lm->out[b].memsz;
    }
    for (int b = OUT_DATA; b <= OUT_BSS; b++) {
        if (lm->out[b].memsz == 0) continue;
        v = alignment(v, lm->out[b].align);
        lm->out[b].vaddr = v;
        v += lm->out[b].memsz;
    }
}

void layout_free_all(link_layout *L) {      //free layout metadata
    if (!L) return;
    if (L->place) {
        for (size_t oi = 0; oi < L->nobj; oi++) free(L->place[oi]);
    }
    free(L->place);
    free(L->shnum);
    memset(L, 0, sizeof(*L));
}

void layout_assign_addresses_all(link_layout *L, uint64_t file_base, uint64_t vaddr_base) {
    uint64_t cur = file_base;
    for (int b = 0; b < OUT_COUNT; b++) {
        if (b == OUT_BSS) continue;
        if (L->out[b].filesz == 0) continue;
        cur = alignment(cur, L->out[b].align);
        L->out[b].file_off = cur;
        cur += L->out[b].filesz;
    }

    uint64_t v = vaddr_base;
    for (int b = OUT_TEXT; b <= OUT_RODATA; b++) {
        if (L->out[b].memsz == 0) continue;
        v = alignment(v, L->out[b].align);
        L->out[b].vaddr = v;
        v += L->out[b].memsz;
    }
    for (int b = OUT_DATA; b <= OUT_BSS; b++) {
        if (L->out[b].memsz == 0) continue;
        v = alignment(v, L->out[b].align);
        L->out[b].vaddr = v;
        v += L->out[b].memsz;
    }
}
