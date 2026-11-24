#pragma once
#include <stdint.h>
// Parches propios del juego: imports que pisan a los genéricos (patch_imports) y hooks/parches de
// __TEXT por dirección del Mach-O, antes de macho_finalize().
void patch_init(void);
void patch_take_bez_stats(uint32_t *hits, uint32_t *misses);
