#ifndef RELOCATION_H
#define RELOCATION_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include "allocation.h"
#include "symbol.h"
#include <elf.h>

int apply_relocations_arm(const input_object *objects, size_t nobj, size_t oi, const global_stable *g, const link_layout *L, out_image *img);

#endif