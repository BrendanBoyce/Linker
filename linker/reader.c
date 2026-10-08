#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include "allocation.h"
#include "relocation.h"
#include "symbol.h"
#include "executable.h"


// descriptions of elf fields used, taken from this Linux manual page:
// https://man7.org/linux/man-pages/man5/elf.5.html

// ELF header (Ehdr)
//        The ELF header is described by the type Elf32_Ehdr or Elf64_Ehdr:

// e_ident
// This array of bytes specifies how to interpret the file,
// independent of the processor or the file's remaining
// contents.  Within this array everything is named by macros,
// which start with the prefix EI_ and may contain values
// which start with the prefix ELF.  The following macros are:
// EI_MAG0
// The first byte of the magic number.  It must be
// filled with ELFMAG0.  (0: 0x7f)

// EI_MAG1
// The second byte of the magic number.  It must be
// filled with ELFMAG1.  (1: 'E')

// EI_MAG2
// The third byte of the magic number.  It must be
// filled with ELFMAG2.  (2: 'L')

// EI_MAG3
// The fourth byte of the magic number.  It must be
// filled with ELFMAG3.  (3: 'F')

// EI_CLASS
// The fifth byte identifies the architecture for this
// binary:

// ELFCLASSNONE
//        This class is invalid.
// ELFCLASS32
//        This defines the 32-bit architecture.  It
//        supports machines with files and virtual
//        address spaces up to 4 Gigabytes.
// ELFCLASS64
//        This defines the 64-bit architecture.
// EI_NIDENT
// The size of the e_ident array.

// e_entry
//   This member gives the virtual address to which the system
//   first transfers control, thus starting the process.  If the
//   file has no associated entry point, this member holds zero.


// e_shoff
//   This member holds the section header table's file offset in
//   bytes.  If the file has no section header table, this
//   member holds zero.


// e_ehsize
//   This member holds the ELF header's size in bytes.



//   e_shnum
//   This member holds the number of entries in the section
//   header table.  Thus the product of e_shentsize and e_shnum
//   gives the section header table's size in bytes.  If a file
//   has no section header table, e_shnum holds the value of
//   zero.

//   If the number of entries in the section header table is
//   larger than or equal to SHN_LORESERVE (0xff00), e_shnum
//   holds the value zero and the real number of entries in the
//   section header table is held in the sh_size member of the
//   initial entry in section header table.  Otherwise, the
//   sh_size member of the initial entry in the section header
//   table holds the value zero.


// e_shstrndx
//   This member holds the section header table index of the
//   entry associated with the section name string table.  If
//   the file has no section name string table, this member
//   holds the value SHN_UNDEF.

//   If the index of section name string table section is larger
//   than or equal to SHN_LORESERVE (0xff00), this member holds
//   SHN_XINDEX (0xffff) and the real index of the section name
//   string table section is held in the sh_link member of the
//   initial entry in section header table.  Otherwise, the
//   sh_link member of the initial entry in section header table
//   contains the value zero.

// e_phnum
// This member holds the number of entries in the program
// header table.  Thus the product of e_phentsize and e_phnum
// gives the table's size in bytes.  If a file has no program
// header, e_phnum holds the value zero.

// If the number of entries in the program header table is
// larger than or equal to PN_XNUM (0xffff), this member holds
// PN_XNUM (0xffff) and the real number of entries in the
// program header table is held in the sh_info member of the
// initial entry in section header table.  Otherwise, the
// sh_info member of the initial entry contains the value
// zero.

// PN_XNUM
//        This is defined as 0xffff, the largest number
//        e_phnum can have, specifying where the actual number
//        of program headers is assigned.

// p_type This member of the structure indicates what kind of segment
// this array element describes or how to interpret the array
// element's information.

//    PT_NULL
//           The array element is unused and the other
//           members' values are undefined.  This lets the
//           program header have ignored entries.

//    PT_LOAD
//           The array element specifies a loadable segment,
//           described by p_filesz and p_memsz.  The bytes
//           from the file are mapped to the beginning of the
//           memory segment.  If the segment's memory size
//           p_memsz is larger than the file size p_filesz,
//           the "extra" bytes are defined to hold the value 0
//           and to follow the segment's initialized area.
//           The file size may not be larger than the memory
//           size.  Loadable segment entries in the program
//           header table appear in ascending order, sorted on
//           the p_vaddr member.

//    PT_DYNAMIC
//           The array element specifies dynamic linking
//           information.

//    PT_INTERP
//           The array element specifies the location and size
//           of a null-terminated pathname to invoke as an
//           interpreter.  This segment type is meaningful
//           only for executable files (though it may occur
//           for shared objects).  However it may not occur
//           more than once in a file.  If it is present, it
//           must precede any loadable segment entry.

//    PT_NOTE
//           The array element specifies the location of notes
//           (ElfN_Nhdr).

//    PT_SHLIB
//           This segment type is reserved but has unspecified
//           semantics.  Programs that contain an array
//           element of this type do not conform to the ABI.

//    PT_PHDR
//           The array element, if present, specifies the
//           location and size of the program header table
//           itself, both in the file and in the memory image
//           of the program.  This segment type may not occur
//           more than once in a file.  Moreover, it may occur
//           only if the program header table is part of the
//           memory image of the program.  If it is present,
//           it must precede any loadable segment entry.

//    PT_LOPROC
//    PT_HIPROC
//           Values in the inclusive range [PT_LOPROC,
//           PT_HIPROC] are reserved for processor-specific
//           semantics.

//    PT_GNU_STACK
//           GNU extension which is used by the Linux kernel
//           to control the state of the stack via the flags
//           set in the p_flags member.



void layout_assign_addresses(layout_map *lm, uint64_t file_base, uint64_t vaddr_base);  //assigns final file offsets

const char *ph_type[] = {       //lookup table for program headers
    [PT_NULL] = "NULL",
    [PT_LOAD] = "LOAD",
    [PT_DYNAMIC] = "DYNAMIC",
    [PT_INTERP] = "INTERP",
    [PT_NOTE] = "NOTE",
    [PT_PHDR] = "PHDR",
};


const char *p_type(uint32_t type) {
    if (type < sizeof(ph_type)/sizeof(ph_type[0]) && ph_type[type] != NULL)
        return ph_type[type];
    return "OTHER";
}

void symbol(FILE *f, const Elf32_Ehdr *ehdr, const Elf32_Shdr *sh_table, const char *sh_strs); // for single input object

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Usage: %s a.o b.o ...\n", argv[0]);
        return 1;
    }

    global_stable *g = gs_new();        //global symbol table for input objects
    size_t nobj = (size_t)(argc - 1);
    input_object *objects = calloc(nobj, sizeof(*objects));
    if (!objects) { 
        perror("calloc objects"); 
        return 1; 
    }


    for (size_t oi = 0; oi < nobj; oi++) {      // pass 1, open objects and validate them, read headers, extract symbols and register in gstable
        const char *path = argv[oi + 1];
        FILE *f = fopen(path, "rb");
        printf("\n=== %s ===\n", path);
        if (!f) { 
            perror("fopen"); 
            continue; 
        }

        input_object *o = &objects[oi];
        o->path = path;
        o->f = f;

        if (fseek(f, 0, SEEK_SET) != 0) { 
            perror("fseek 0"); 
            fclose(f); 
            o->f=NULL; 
            continue; 
        }

        if (fread(&o->ehdr, 1, sizeof(o->ehdr), f) != sizeof(o->ehdr)) {    //validation
            fprintf(stderr, "%s: failed to read ELF header\n", path);
            fclose(f); o->f = NULL;
            continue;
        }

        if (memcmp(o->ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
            fprintf(stderr, "%s: not an ELF file (bad magic)\n", path);
            fclose(f); o->f = NULL;
            continue;
        }

        printf("Magic:   ");
        for (int i = 0; i < EI_NIDENT; i++) printf("%02x ", o->ehdr.e_ident[i]);
        printf("\n");

        printf("DEBUG: %s ftell after ehdr read = %ld\n", path, ftell(f));
        printf("DEBUG: magic bytes = %02x %02x %02x %02x\n",
            o->ehdr.e_ident[0], o->ehdr.e_ident[1], o->ehdr.e_ident[2], o->ehdr.e_ident[3]);

        printf("Class:   %s\n",
            o->ehdr.e_ident[EI_CLASS] == ELFCLASS32 ? "ELF32" :
            o->ehdr.e_ident[EI_CLASS] == ELFCLASS64 ? "ELF64" : "Invalid");

        printf("Data:    %s endian\n",
            o->ehdr.e_ident[EI_DATA] == ELFDATA2LSB ? "Little" :
            o->ehdr.e_ident[EI_DATA] == ELFDATA2MSB ? "Big" : "Unknown");


        printf("Version: %d\n", o->ehdr.e_ident[EI_VERSION]);
        printf("OS/ABI:  %d\n", o->ehdr.e_ident[EI_OSABI]);
        printf("ABI Ver: %d\n", o->ehdr.e_ident[EI_ABIVERSION]);



        // if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        //     ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        //     ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        //     ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        //     printf("Not an ELF file.\n");
        //     fclose(f);
        //     return 1;
        // }

        printf("ELF Header:\n");
        printf("  Class: %s\n", o->ehdr.e_ident[EI_CLASS] == ELFCLASS32 ? "32-bit" : "NOT-ELF32");
        printf("  Type: %d\n", o->ehdr.e_type);
        printf("  Machine: %d\n", o->ehdr.e_machine);
        printf("  Entry point: 0x%lx\n", o->ehdr.e_entry);
        printf("  Section header offset: %ld\n", o->ehdr.e_shoff);
        printf("  Number of section headers: %d\n", o->ehdr.e_shnum);
        printf("  Section header string table index: %d\n", o->ehdr.e_shstrndx);
        
        if (o->ehdr.e_type != ET_REL) {
            fprintf(stderr, "%s: unsupported ELF type %u (expected ET_REL)\n", path, (unsigned)o->ehdr.e_type);  //make life easier
            fclose(f); o->f = NULL;
            continue;
        }

        if (o->ehdr.e_shoff == 0 || o->ehdr.e_shnum == 0) {
            fprintf(stderr, "%s: no section headers exist\n", path);
            fclose(f); o->f = NULL;
            continue;
        }

        o->sh_table = malloc((size_t)o->ehdr.e_shnum * sizeof(*o->sh_table));   //read into memory
        if (!o->sh_table) { 
            perror("malloc sh_table"); 
            fclose(f); o->f=NULL; 
            continue; 
        }

        if (fseek(f, (long)o->ehdr.e_shoff, SEEK_SET) != 0) {
            perror("fseek shoff");
            free(o->sh_table); o->sh_table=NULL;
            fclose(f); o->f=NULL;
            continue;
        }
        if (fread(o->sh_table, sizeof(Elf32_Shdr), o->ehdr.e_shnum, f) != o->ehdr.e_shnum) {
            fprintf(stderr, "%s: failed to read section headers\n", path);
            free(o->sh_table); o->sh_table=NULL;
            fclose(f); o->f=NULL;
            continue;
        }

        if (o->ehdr.e_ident[EI_CLASS] != ELFCLASS32) {      // no 64 please
            fprintf(stderr, "%s: not ELF32 (class=%u)\n",
                    path, o->ehdr.e_ident[EI_CLASS]);
            fclose(f);
            o->f = NULL;
            continue;
        }

        if (o->ehdr.e_ehsize != sizeof(Elf32_Ehdr)) {   //sanity
            fprintf(stderr, "%s: unexpected e_ehsize %u (expected %zu)\n",
                    path,
                    (unsigned)o->ehdr.e_ehsize,
                    sizeof(Elf32_Ehdr));
            fclose(f);
            o->f = NULL;
            continue;
        }


        Elf32_Shdr sh_strtab = o->sh_table[o->ehdr.e_shstrndx];     //load string table
        o->sh_strs = malloc((size_t)sh_strtab.sh_size);
        if (!o->sh_strs) { perror("malloc sh_strs"); free(o->sh_table); o->sh_table=NULL; fclose(f); o->f=NULL; continue; }

        if (fseek(f, (long)sh_strtab.sh_offset, SEEK_SET) != 0) {
            perror("fseek sh_strtab");
            free(o->sh_strs); o->sh_strs=NULL;
            free(o->sh_table); o->sh_table=NULL;
            fclose(f); o->f=NULL;
            continue;
        }
        if (fread(o->sh_strs, 1, (size_t)sh_strtab.sh_size, f) != (size_t)sh_strtab.sh_size) {
            fprintf(stderr, "%s: failed to read shstrtab\n", path);
            free(o->sh_strs); o->sh_strs=NULL;
            free(o->sh_table); o->sh_table=NULL;
            fclose(f); o->f=NULL;
            continue;
        } //new up

        //symbol table
        symbol(f, &o->ehdr, o->sh_table, o->sh_strs);


        Elf32_Phdr *phdrs = NULL;   //debug

        if (o->ehdr.e_type != ET_REL && o->ehdr.e_phoff != 0 && o->ehdr.e_phnum != 0) {
            phdrs = malloc((size_t)o->ehdr.e_phnum * sizeof(*phdrs));
            if (!phdrs) { 
                perror("malloc"); 
                exit(1); 
            }

            if (fseek(f, (long)o->ehdr.e_phoff, SEEK_SET) != 0) { 
                perror("fseek"); 
                exit(1); 
            }

            if (fread(phdrs, sizeof(*phdrs), o->ehdr.e_phnum, f) != o->ehdr.e_phnum) {
                fprintf(stderr, "Failed to read program headers\n");
                exit(1);
            }

        //parse headers 
        printf("Program Headers:\n");
        for (int i = 0; i < (int)o->ehdr.e_phnum; i++) {
                printf("[%2d] %-7s off 0x%lx vaddr 0x%lx filesz 0x%lx memsz 0x%lx flags 0x%x align 0x%lx\n",
                    i,
                    p_type(phdrs[i].p_type),
                    (unsigned long)phdrs[i].p_offset, //holds offset from beginning of file where first byte is
                    (unsigned long)phdrs[i].p_vaddr, //holds virtual address of where first byte is
                    (unsigned long)phdrs[i].p_filesz, // number of bytes in file image
                    (unsigned long)phdrs[i].p_memsz, // number of bytes in memory image
                    phdrs[i].p_flags, // bit mask of flags
                    (unsigned long)phdrs[i].p_align); //value to which segments are aligned in memory and file
            }
            free(phdrs);
        }

        if (gs_add_object(g, path, f, &o->ehdr, o->sh_table, o->sh_strs) != 0) {    // register object symbols with gstable
        fprintf(stderr, "%s: Symbol collection failed\n", path);
            free(objects[oi].sh_strs);
            free(objects[oi].sh_table);
            fclose(f);
            gs_free(g);
            return 1;
        }
        
        printf("Section Headers:\n");
        for (int i = 0; i < (int)objects[oi].ehdr.e_shnum; i++) {
            printf("[%2d] %s (offset 0x%lx, size %lu bytes)\n",
            i,
            &objects[oi].sh_strs[objects[oi].sh_table[i].sh_name],
            (unsigned long)objects[oi].sh_table[i].sh_offset,
            (unsigned long)objects[oi].sh_table[i].sh_size);
        }


    }

    if (gs_finish(g) != 0) {    //law enforcment, table definitons
        gs_free(g);
        return 1;
    }

    link_layout L;
    if (layout_build_all(&L, objects, nobj) != 0) {    // pass 2
        fprintf(stderr, "layout_build_all failed\n");
        // cleanup objects before exiting
        for (size_t oi = 0; oi < nobj; oi++) {
            if (objects[oi].f) fclose(objects[oi].f);
            free(objects[oi].sh_table);
            free(objects[oi].sh_strs);
        }
        free(objects);
        gs_free(g);
        return 1;
    }

    layout_assign_addresses_all(&L, 0x1000, 0x400000);  //assign file offsets and virtual address

    out_image img;
    if (build_out_image_all(&img, objects, nobj, &L) != 0) {
        fprintf(stderr, "build_out_image_all failed\n");
        layout_free_all(&L);
        for (size_t oi = 0; oi < nobj; oi++) {
            if (objects[oi].f) fclose(objects[oi].f);
            free(objects[oi].sh_table);
            free(objects[oi].sh_strs);
        }
        free(objects);
        gs_free(g);
        return 1;
    }

    for (size_t oi = 0; oi < nobj; oi++) {      //apply relocation
        if (!objects[oi].f) continue;
        if (apply_relocations_arm(objects, nobj, oi, g, &L, &img) != 0) {
            fprintf(stderr, "%s: relocation failed\n", objects[oi].path);
            free_out_image(&img);
            layout_free_all(&L);
            for (size_t j = 0; j < nobj; j++) {
                if (objects[j].f) fclose(objects[j].f);
                free(objects[j].sh_table);
                free(objects[j].sh_strs);
            }
            free(objects);
            gs_free(g);
            return 1;
        }
    }

    printf("All relocations applied (text=%lu rodata=%lu data=%lu)\n",  //summary of output image
        (unsigned long)img.size[OUT_TEXT],
        (unsigned long)img.size[OUT_RODATA],
        (unsigned long)img.size[OUT_DATA]);
    
    if (write_executable("a.out", objects, nobj, g, &L, &img, 0x1000) != 0) {
        fprintf(stderr, "Failed to write executable\n");
    }
    
    free_out_image(&img);   //cleanup
    layout_free_all(&L);

    for (size_t oi = 0; oi < nobj; oi++) {
        if (objects[oi].f) fclose(objects[oi].f);
        free(objects[oi].sh_table);
        free(objects[oi].sh_strs);
    }
    free(objects);
    gs_free(g);
    return 0;
}

