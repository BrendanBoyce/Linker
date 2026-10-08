#include <elf.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "allocation.h"
#include "symbol.h"

// EI_CLASS
//                      The fifth byte identifies the architecture for this
//                      binary:

//                      ELFCLASSNONE
//                             This class is invalid.
//                      ELFCLASS32
//                             This defines the 32-bit architecture.  It
//                             supports machines with files and virtual
//                             address spaces up to 4 Gigabytes.
//                      ELFCLASS64
//                             This defines the 64-bit architecture.

//               EI_DATA
//                      The sixth byte specifies the data encoding of the
//                      processor-specific data in the file.  Currently,
//                      these encodings are supported:

//                        ELFDATANONE
//                               Unknown data format.
//                        ELFDATA2LSB
//                               Two's complement, little-endian.
//                        ELFDATA2MSB
//                               Two's complement, big-endian.
//  EI_OSABI
//                      The eighth byte identifies the operating system and
//                      ABI to which the object is targeted.  Some fields in
//                      other ELF structures have flags and values that have
//                      platform-specific meanings; the interpretation of
//                      those fields is determined by the value of this
//                      byte.  For example:

//                      ELFOSABI_NONE
//                             Same as ELFOSABI_SYSV
//                      ELFOSABI_SYSV
//                             UNIX System V ABI
//                      ELFOSABI_HPUX
//                             HP-UX ABI
//                      ELFOSABI_NETBSD
//                             NetBSD ABI
//                      ELFOSABI_LINUX
//                             Linux ABI
//                      ELFOSABI_SOLARIS
//                             Solaris ABI
//                      ELFOSABI_IRIX
//                             IRIX ABI
//                      ELFOSABI_FREEBSD
//                             FreeBSD ABI
//                      ELFOSABI_TRU64
//                             TRU64 UNIX ABI
//                      ELFOSABI_ARM
//                             ARM architecture ABI
//                      ELFOSABI_STANDALONE
//                             Stand-alone (embedded) ABI

static int write_zeros(FILE *out, uint32_t n) {     //utility for padding and alignment
    static uint8_t z[4096];
    while (n > 0) {
        uint32_t chunk = (n > sizeof(z)) ? (uint32_t)sizeof(z) : n;
        if (fwrite(z, 1, chunk, out) != chunk) return -1;
        n -= chunk;
    }
    return 0;
}

static int find_obj_index_by_path(const input_object *objects, size_t nobj, const char *path) {
    for (size_t i = 0; i < nobj; i++) {
        if (objects[i].path && strcmp(objects[i].path, path) == 0){ 
            return (int)i;
        }
    }
    return -1;
}

static int get_symbol_addr(uint32_t *out_addr, const char *name, const input_object *objects, size_t nobj, const global_stable *g, const link_layout *L) {
    const g_symbol *gs = gs_lookup(g, name);    //compute runtime address of global symbol
    if (!gs || !gs->is_defined){
        return -1;
    }

    if (gs->shndx == SHN_ABS) {
        *out_addr = (uint32_t)gs->value;
        return 0;
    }

    int def_oi = find_obj_index_by_path(objects, nobj, gs->from);  //find input object that defines symbol
    if (def_oi < 0) {
        return -1;
    }

    if ((size_t)gs->shndx >= L->shnum[def_oi]) {    //validate
        return -1;
    }
    if (!L->place[def_oi][gs->shndx].used) {
        return -1;
    }

    int b = (int)L->place[def_oi][gs->shndx].bucket;
    uint32_t sec_off = (uint32_t)L->place[def_oi][gs->shndx].out_off;

    *out_addr = (uint32_t)(L->out[b].vaddr + (uint64_t)sec_off + (uint64_t)gs->value);  //compute final address
    return 0;
}
//write the file out
int write_executable(const char *out_path, const input_object *objects, size_t nobj, const global_stable *g,  const link_layout *L,  const out_image *img, uint32_t file_base) {
    uint32_t entry = 0;     
    if (get_symbol_addr(&entry, "_start", objects, nobj, g, L) != 0) {
        if (get_symbol_addr(&entry, "main", objects, nobj, g, L) != 0) {
            fprintf(stderr, "No entry symbol found (_start or main)\n");
            return -1;
        }
    }

    // One pt load segment covering everything
    uint32_t seg_off  = file_base;
    uint32_t seg_vaddr = (uint32_t)L->out[OUT_TEXT].vaddr;

    uint32_t file_end = seg_off;        //compute segment file size
    for (int b = 0; b < OUT_COUNT; b++) {
        if (b == OUT_BSS){
            continue;
        }
        if (L->out[b].filesz == 0) { 
            continue;
        }
        uint32_t end = (uint32_t)(L->out[b].file_off + L->out[b].filesz);
        if (end > file_end) file_end = end;
    }
    uint32_t seg_filesz = file_end - seg_off;

    uint32_t mem_end = seg_vaddr;   //compute segment memory size
    for (int b = 0; b < OUT_COUNT; b++) {
        if (L->out[b].memsz == 0){ 
            continue;
        }
        uint32_t end = (uint32_t)(L->out[b].vaddr + L->out[b].memsz);
        if (end > mem_end) mem_end = end;
    }
    uint32_t seg_memsz = mem_end - seg_vaddr;

    Elf32_Ehdr eh;  //build elf header
    memset(&eh, 0, sizeof(eh));
    memcpy(eh.e_ident, ELFMAG, SELFMAG);
    eh.e_ident[EI_CLASS] = ELFCLASS32;
    eh.e_ident[EI_DATA]  = ELFDATA2LSB;
    eh.e_ident[EI_VERSION] = EV_CURRENT;
    eh.e_ident[EI_OSABI] = ELFOSABI_SYSV;

    eh.e_type    = ET_EXEC;
    eh.e_machine = EM_ARM;
    eh.e_version = EV_CURRENT;
    eh.e_entry   = entry;
    eh.e_phoff   = sizeof(Elf32_Ehdr);
    eh.e_ehsize  = sizeof(Elf32_Ehdr);
    eh.e_phentsize = sizeof(Elf32_Phdr);
    eh.e_phnum   = 1;

    if (nobj > 0) eh.e_flags = objects[0].ehdr.e_flags;

    Elf32_Phdr ph;      //build single pt load to cover whole image
    memset(&ph, 0, sizeof(ph));
    ph.p_type   = PT_LOAD;
    ph.p_offset = seg_off;
    ph.p_vaddr  = seg_vaddr;
    ph.p_paddr  = seg_vaddr;
    ph.p_filesz = seg_filesz;
    ph.p_memsz  = seg_memsz;
    ph.p_flags  = PF_R | PF_W | PF_X;
    ph.p_align  = 0x1000;

    FILE *out = fopen(out_path, "wb");                  //open output file and write headers
    if (!out) { perror("fopen output");
        return -1; 
    }

    if (fwrite(&eh, 1, sizeof(eh), out) != sizeof(eh)) { 
        fclose(out); 
        return -1; 
    }
    if (fwrite(&ph, 1, sizeof(ph), out) != sizeof(ph)) { 
        fclose(out); 
        return -1; 
    }

    long cur = ftell(out);              //pad from end of headers to segment start
    if (cur < 0) { 
        fclose(out); 
        return -1; 
    }

    if ((uint32_t)cur > file_base) {
        fprintf(stderr, "file_base too small (headers exceed it)\n");
        fclose(out);
        return -1;
    }

    if (write_zeros(out, file_base - (uint32_t)cur) != 0) { fclose(out); return -1; }

    for (int b = 0; b < OUT_COUNT; b++) { // write buckets at the file offsets computed in layout address asignment
        if (b == OUT_BSS){ 
            continue;
        }
        if (L->out[b].filesz == 0){ 
            continue;
        }

        if (fseek(out, (long)L->out[b].file_off, SEEK_SET) != 0) { 
            perror("fseek out"); 
            fclose(out); 
            return -1; 
        }

        uint32_t want = (uint32_t)L->out[b].filesz;
        uint32_t have = (uint32_t)img->size[b];
        if (have < want) {
            fprintf(stderr, "bucket %d buffer too small (have=%u want=%u)\n", b, have, want);
            fclose(out);
            return -1;
        }

        if (fwrite(img->buf[b], 1, want, out) != want) { fclose(out); return -1; }
    }

    fclose(out);
    printf("Wrote executable: %s (entry=0x%08x)\n", out_path, entry);   //finito
    return 0;
}
