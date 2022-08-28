
# Memory space structure

Space is structured in arenas for 48bit virtual memory space (so 64bit cpu).

## Arena

An arena is a region slab of 4Gb, region size is based on 2^n, with n in [16, 32].
So it exists 16 kind of region size and arena templates.

Space is organized around a arenas descriptor pointer table, each descriptor have a table of region 8bit tag.

## Region



Space is structure in specialize region:
- __Descriptors_Region__: is a region which contains descriptor of other regions
- __Objects_Region__: is region which contains a object table, each region is specialized for one object classsize
- __Types_Region__: is region which contains a generic types table
- __Memory_Map_Region__: is a region which contains a translation table from address to the region descriptor

