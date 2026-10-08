# Architecture

## Pipeline and shared metadata

`reader.c` owns the command-line workflow. Each argument names an object file; the program opens it, reads its ELF32 header and section table, loads the section-name string table, prints diagnostics, and adds external symbols to a shared global table.

After symbol resolution, it constructs an output layout, assigns addresses, copies section bytes, applies relocations for each accepted object, and calls `write_executable` with the fixed output name `a.out`. Normal completion frees buffers, placement tables, input metadata, file handles, and the global symbol table. Some early failure paths have less complete cleanup.

| Structure | Responsibility |
| --- | --- |
| `input_object` | Input path, open stream, ELF header, section headers, and section-name strings |
| `global_stable` / `g_symbol` | Growable collection of external names, bindings, definition state, source objects, section indexes, values, and sizes |
| `link_layout` | Per-object section placements and aggregate bucket sizes, alignments, offsets, and addresses |
| `section_placement` | Whether a section is included, its output bucket, and its offset inside that bucket |
| `out_image` | File-backed byte buffers for output buckets |

The multi-object pipeline uses `link_layout`. The older `layout_map` type and diagnostic helpers remain in the source but are not the primary linking path.

## Symbol resolution

`gs_add_object` locates `.symtab`, follows its linked string table, and passes external symbols to `resolve_insert`. Local symbols and file/section symbols are excluded from the global table.

Resolution follows these rules:

1. A previously unseen name is inserted, whether defined or undefined.
2. A later undefined declaration cannot improve an existing entry.
3. A definition replaces an unresolved entry.
4. Two strong definitions produce an error.
5. A strong definition replaces a weak definition; a weak definition does not replace a strong one.

`gs_finish` prints the table and fails if any entries remain undefined, including undefined weak entries. Name searches use linear scans. Common symbols are recorded as defined but have no separate allocation pass.

## Section allocation and image construction

`layout_build_all` includes sections with `SHF_ALLOC` whose names start with `.text`, `.rodata`, or `.data`; `.bss` and other remaining `SHT_NOBITS` sections use the BSS bucket. Unrecognized sections are omitted.

For each included section, the bucket's current size is rounded up to the section alignment. That offset is recorded in the placement table and the bucket is enlarged. The output alignment is the maximum input alignment encountered in the bucket.

`layout_assign_addresses_all` assigns file offsets beginning at `0x1000` and virtual addresses beginning at `0x400000`. BSS consumes virtual memory but no file bytes. These two alignment sequences are calculated independently.

`build_out_image_all` allocates zero-filled buffers for text, read-only data, and initialized data, then copies input section bytes to their recorded offsets. Padding remains zero. BSS has no buffer.

For a placed symbol, the intended final address is:

```text
symbol address = output bucket virtual address
               + defining input section's bucket offset
               + symbol's section-relative value
```

Absolute symbols use their recorded value directly.

## ARM relocation

`apply_relocations_arm` scans `SHT_REL` sections, uses `sh_info` to identify the target section and `sh_link` to locate the symbol table, then reads the relocation entries and symbol strings. It checks several indexes and patch bounds before changing the output image.

The patch address combines the target section's placement offset with the relocation's offset. The implementation resolves referenced names through the global table, so local and section-symbol references cannot be resolved through that path.

For `R_ARM_CALL`, it reads the 32-bit instruction, sign-extends the low 24-bit immediate, shifts it by two to obtain the encoded addend, calculates a new displacement, checks alignment/range, and replaces the immediate. `R_ARM_NONE` leaves bytes unchanged, although the common symbol-resolution path still runs first. Other relocation types fail explicitly; `SHT_RELA` sections are not processed.

### Confirmed branch-target defect

For the supplied `foo.o` followed by `main.o`, the relocation site is `P = 0x00400024`, `foo` is at `S = 0x00400000`, and the original instruction is `0xebfffffe`. Its encoded addend is `-8`.

The implementation calculates `S + A - (P + 8)`, producing instruction `0xebfffff3`. Decoding the ARM branch using its execution-time PC value gives:

```text
actual target = P + 8 + sign_extend(0xfffff3) × 4
              = 0x003ffff8
```

This is eight bytes before the intended symbol. The diagnostic “All relocations applied” records pass completion, not semantic correctness. The defect is documented without modifying source code.

## Executable output

`write_executable` searches for `_start`, then `main`, and resolves the selected entry through the global table and placement metadata. It writes an `ET_EXEC`, `ELFCLASS32`, little-endian, `EM_ARM` header, copying ELF flags from the first input object.

One `PT_LOAD` segment begins at file offset `0x1000` with alignment `0x1000` and read/write/execute permissions. Its file extent covers the file-backed buckets; its memory extent includes BSS. Headers precede padded content. No section headers or output symbol table are emitted.

There is no startup runtime, dynamic interpreter, library loader, or process-exit scaffold. The file is created with ordinary write permissions, and executable-writing errors are printed without changing the driver's final success status. Inspection of the generated ELF is necessary in addition to checking the process exit code.

## Examples and verification boundary

`foo.c` and `main.c` provide a minimal external function reference. Their committed object files are ARM32 ELF inputs. `hello.c` is a separate libc-based example; its committed object file is ELF64 x86-64 and is outside the linker's supported workflow.

Documentation review rebuilt the linker from source, linked the supplied ARM objects into a temporary directory, inspected the result with `readelf`, verified unresolved-symbol rejection, and independently decoded the patched branch. It did not execute the output or rebuild the ARM objects. No executable code or binary contents were changed when the implementation directory was renamed to `linker`.
