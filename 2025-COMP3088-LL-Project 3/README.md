# COMP3088 Linkers and Loaders Small Group Study Project

This is my implementation of the coursework for linkers and loaders.

This if for an ARM32 machine and for ELF object files.

This linker can:

1) parse input ELF object files
2) collect and resolve global symbols
3) compute global layout for allocatable sections
4) apply relocations
5) output an executable file

compile with: gcc reader.c symbol.c allocation.c relocation_arm.c executable.c -o reader

and compile foo.c and main.c to be relocatable and not automatically linked

run with: ./reader foo.o main.o

you will see these object files become linked.

this is a very basic linker and unfortunately does not support libraries





