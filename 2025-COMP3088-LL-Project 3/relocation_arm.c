#include "relocation.h"
#include "allocation.h"
#include "symbol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <elf.h>


static uint32_t read_u32(const uint8_t *p) {    //utility
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void write_u32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, 4);
}

static int32_t sign_extend_24_to_32(uint32_t encoded_offset) {   // extend to 32 bits
    if (encoded_offset & 0x00800000) {
        return (int32_t)(encoded_offset | 0xFF000000);
    }
    return (int32_t)encoded_offset;
}

static int apply_r_arm_call(const char *path, const char *sym_name, uint8_t *loc, uint32_t P, uint32_t S) {     //decode existing branch and encode again
    uint32_t instr = read_u32(loc);

    uint32_t encoded_offset = instr & 0x00FFFFFF;
    int32_t  sencoded_offset = sign_extend_24_to_32(encoded_offset);
    int32_t  A = sencoded_offset << 2;

    int32_t disp = (int32_t)S + A - (int32_t)(P + 8);

    if ((disp & 3) != 0) {
        fprintf(stderr, "%s: R_ARM_CALL unaligned disp for %s (disp=%d)\n", path, sym_name, disp);
        return -1;
    }

    int32_t disp_words = disp >> 2;

    if (disp_words < -(1 << 23) || disp_words > ((1 << 23) - 1)) {
        fprintf(stderr, "%s: R_ARM_CALL out of range for %s (disp_words=%d)\n", path, sym_name, disp_words);
        return -1;
    }

    uint32_t new_encoded_offset = (uint32_t)disp_words & 0x00FFFFFF;
    instr = (instr & 0xFF000000) | new_encoded_offset;
    write_u32(loc, instr);
    return 0;
}

static int find_obj_index_by_path(const input_object *objects, size_t nobj, const char *path) {    //helper
    for (size_t i = 0; i < nobj; i++) {
        if (objects[i].path && strcmp(objects[i].path, path) == 0){
            return (int)i;
        }
    }
    return -1;
}

static int is_file_backed_bucket(int b) {       // find what outputs are in the image
    return (b == OUT_TEXT || b == OUT_RODATA || b == OUT_DATA);
}

//apply the relocations for objects

int apply_relocations_arm(const input_object *objects, size_t nobj, size_t oi, const global_stable *g, const link_layout *L, out_image *img) {

    const input_object *o = &objects[oi];          //bind metadata
    const Elf32_Ehdr *ehdr = &o->ehdr;
    const Elf32_Shdr *sh_table = o->sh_table;
    const char *sh_strs = o->sh_strs;

    if (!o->f || !sh_table || !sh_strs) {   // no relocations
        return 0;
    }

    printf("DEBUG: apply_relocations_arm on %s\n", o->path);

    int relsec_found = 0;   // counting how many processed vs found
    int rel_processed = 0;

    for (int rsi = 0; rsi < (int)ehdr->e_shnum; rsi++) {    // find sections looking for relocation
        const Elf32_Shdr *rsec = &sh_table[rsi];
        if (rsec->sh_type != SHT_REL) {
            continue;
        }

        const char *rsec_name = &sh_strs[rsec->sh_name];    // getname and symtab reference
        relsec_found++;

        uint32_t target_sec_index = rsec->sh_info;
        uint32_t symtab_index = rsec->sh_link;

        if (target_sec_index >= ehdr->e_shnum || symtab_index >= ehdr->e_shnum) {   //validate with metadata
            fprintf(stderr, "%s: bad relocation section links (sh_info=%u sh_link=%u)\n", o->path, target_sec_index, symtab_index);
            return -1;
        }

        const Elf32_Shdr *tsec = &sh_table[target_sec_index];
        const char *tsec_name = &sh_strs[tsec->sh_name];

        const Elf32_Shdr *symtab = &sh_table[symtab_index];     //validate relocations have a place in symtab
        if (symtab->sh_type != SHT_SYMTAB) {
            fprintf(stderr, "%s: relocation links to non-SYMTAB section\n", o->path);
            return -1;
        }

        if (symtab->sh_link >= ehdr->e_shnum) {     //get string table for names
            fprintf(stderr, "%s: symtab has bad sh_link\n", o->path);
            return -1;
        }

        const Elf32_Shdr *strtab = &sh_table[symtab->sh_link];

        // read symbol string table, load all into memory
        char *sym_strs2 = malloc((size_t)strtab->sh_size);
        if (!sym_strs2) { 
            perror("malloc sym_strs2");
            return -1; 
        }

        if (fseek(o->f, (long)strtab->sh_offset, SEEK_SET) != 0) { 
            perror("fseek strtab"); free(sym_strs2); 
            return -1; 
        }

        if (fread(sym_strs2, 1, (size_t)strtab->sh_size, o->f) != (size_t)strtab->sh_size) {
            fprintf(stderr, "%s: failed reading strtab\n", o->path);
            free(sym_strs2);
            return -1;
        }

        // read symbols
        int nsyms = (int)(symtab->sh_size / sizeof(Elf32_Sym));
        Elf32_Sym *syms = malloc((size_t)nsyms * sizeof(*syms));
        if (!syms) { 
            perror("malloc syms"); 
            free(sym_strs2); 
            return -1; 
        }

        if (fseek(o->f, (long)symtab->sh_offset, SEEK_SET) != 0) { 
            perror("fseek symtab"); 
            free(syms); 
            free(sym_strs2); 
            return -1; 
        }

        if (fread(syms, sizeof(*syms), (size_t)nsyms, o->f) != (size_t)nsyms) {
            fprintf(stderr, "%s: failed reading symtab\n", o->path);
            free(syms); 
            free(sym_strs2);
            return -1;
        }

        // read relocations
        int nrel = (int)(rsec->sh_size / sizeof(Elf32_Rel));
        Elf32_Rel *rels = malloc((size_t)nrel * sizeof(*rels));
        if (!rels) { 
            perror("malloc rels"); 
            free(syms); 
            free(sym_strs2); 
            return -1; 
        }

        if (fseek(o->f, (long)rsec->sh_offset, SEEK_SET) != 0) { 
            perror("fseek relsec"); 
            free(rels); 
            free(syms); 
            free(sym_strs2); 
            return -1; 
        }

        if (fread(rels, sizeof(*rels), (size_t)nrel, o->f) != (size_t)nrel) {
            fprintf(stderr, "%s: failed reading relocations\n", o->path);
            free(rels); 
            free(syms); 
            free(sym_strs2);
            return -1;
        }

        printf("DEBUG: found relsec %s (SHT_REL) applying-to=%s count=%d sh_info=%u sh_link=%u\n",
               rsec_name, tsec_name, nrel, target_sec_index, symtab_index);

        // determine where the target section lives in output
        if (target_sec_index >= L->shnum[oi] || !L->place[oi][target_sec_index].used) {
            fprintf(stderr, "%s: target section %u not placed in output layout\n", o->path, target_sec_index);
            free(rels); 
            free(syms); 
            free(sym_strs2);
            return -1;
        }

        int tbucket = (int)L->place[oi][target_sec_index].bucket;
        uint32_t t_out_off = (uint32_t)L->place[oi][target_sec_index].out_off;

        if (!is_file_backed_bucket(tbucket) || !img->buf[tbucket]) {
            fprintf(stderr, "%s: target bucket not file-backed or missing buffer\n", o->path);
            free(rels); 
            free(syms); 
            free(sym_strs2);
            return -1;
        }

        for (int i = 0; i < nrel; i++) {        // apply each relocation entry
            uint32_t r_offset = rels[i].r_offset;
            uint32_t r_info   = rels[i].r_info;

            uint32_t rsym  = ELF32_R_SYM(r_info);
            uint32_t rtype = ELF32_R_TYPE(r_info);

            if (rsym >= (uint32_t)nsyms) {
                fprintf(stderr, "%s: relocation has bad sym index %u\n", o->path, rsym);
                free(rels); 
                free(syms);
                free(sym_strs2);
                return -1;
            }

            const Elf32_Sym *sym = &syms[rsym];
            const char *sym_name = (sym->st_name < strtab->sh_size) ? &sym_strs2[sym->st_name] : "<badname>";

            printf("DEBUG: rel[%d] off=0x%08x type=%u sym=%u name=%s\n", i, r_offset, rtype, rsym, sym_name);

            // compute location in output image buffer
            uint32_t patch_off = t_out_off + r_offset;
            if (patch_off + 4 > img->size[tbucket]) {
                fprintf(stderr, "%s: patch overflow: bucket=%d patch_off=0x%x size=0x%lx\n", o->path, tbucket, patch_off, (unsigned long)img->size[tbucket]);
                free(rels); 
                free(syms); 
                free(sym_strs2);
                return -1;
            }

            uint8_t *loc = img->buf[tbucket] + patch_off;

            // compute runtime address of relocation site
            uint32_t P = (uint32_t)(L->out[tbucket].vaddr + (uint64_t)patch_off);

            // resolve S using global symbol table
            const g_symbol *gs = gs_lookup(g, sym_name);
            if (!gs || !gs->is_defined) {
                fprintf(stderr, "%s: unresolved symbol %s\n", o->path, sym_name);
                free(rels); free(syms); free(sym_strs2);
                return -1;
            }

            uint32_t S = 0;

            if (gs->shndx == SHN_ABS) {
                // absolute symbol
                S = (uint32_t)gs->value;
            } else if (gs->shndx == SHN_UNDEF) {
                fprintf(stderr, "%s: symbol %s is unexpectedly UNDEF\n", o->path, sym_name);
                free(rels); free(syms); free(sym_strs2);
                return -1;
            } else {
                int def_oi = find_obj_index_by_path(objects, nobj, gs->from);
                if (def_oi < 0) {
                    fprintf(stderr, "%s: cannot locate defining object for %s (from=%s)\n", o->path, sym_name, gs->from);
                    free(rels); free(syms); free(sym_strs2);
                    return -1;
                }

                if ((size_t)gs->shndx >= L->shnum[def_oi] || !L->place[def_oi][gs->shndx].used) {
                    fprintf(stderr, "%s: defining section %u for %s not placed\n", o->path, (unsigned)gs->shndx, sym_name);
                    free(rels); free(syms); free(sym_strs2);
                    return -1;
                }

                int sbucket = (int)L->place[def_oi][gs->shndx].bucket;
                uint32_t s_out_off = (uint32_t)L->place[def_oi][gs->shndx].out_off;

                // S is output vaddr of symbols section and placement offset and symbol st_value
                S = (uint32_t)(L->out[sbucket].vaddr + (uint64_t)s_out_off + (uint64_t)gs->value);
            }

            uint32_t before = read_u32(loc);

            if (rtype == R_ARM_CALL) {
                printf("DEBUG: R_ARM_CALL P=0x%08x S=0x%08x before=0x%08x\n", P, S, before);
                if (apply_r_arm_call(o->path, sym_name, loc, P, S) != 0) {
                    free(rels); free(syms); free(sym_strs2);
                    return -1;
                }
                uint32_t after = read_u32(loc);
                printf("DEBUG: R_ARM_CALL after=0x%08x (changed=%s)\n", after, (after != before) ? "yes" : "no");
            } else if (rtype == R_ARM_NONE) {
                // do nothing
            } else {
                fprintf(stderr, "%s: unsupported ARM relocation type %u (sym=%s)\n", o->path, rtype, sym_name);
                free(rels); free(syms); free(sym_strs2);
                return -1;
            }

            rel_processed++;
        }

        free(rels);
        free(syms);
        free(sym_strs2);
    }

    printf("DEBUG: %s relocation summary: relsec_found=%d rel_processed=%d\n",  //relocation summary for object
           o->path, relsec_found, rel_processed);

    return 0;
}
