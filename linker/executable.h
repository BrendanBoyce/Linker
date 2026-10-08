#ifndef EXECUTABLE_H
#define EXECUTABLE_H

#include <stdint.h>
#include <stddef.h>
#include "allocation.h"
#include "symbol.h"

int write_executable(const char *out_path, const input_object *objects, size_t nobj, const global_stable *g,  const link_layout *L,  const out_image *img, uint32_t file_base);


#endif
