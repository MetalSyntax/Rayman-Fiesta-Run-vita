// Parches de este juego. Cada parche se agrega con evidencia (log/dump + desensamblado) y su
// dirección del Mach-O.
#include <stddef.h>
#include <stdint.h>
#include "imports.h"
#include "log.h"
#include "macho.h"
#include "patch.h"

const import_entry patch_imports[] = {
    {"", 0, 0},
};
TABLE_END(patch)

// Ray_RFR_TeensyUnlockManagerComponent (contador de teensies del mapa): el init (0x3b65ac) llena la
// tabla de UVs (+0x18c cuenta, +0x190/+0x194 SafeArray) desde texture(+0xb4)->uvAtlas(+0x84) una
// sola vez; si la textura todavía no pasó por postCreate el atlas es NULL, la tabla queda vacía y el
// draw (0x3b67d0) cae en el trap de rango en 0x3b6b6a (log/dump 012, TeensyUnlockManager por RTTI).
// Mismo caso que ScoreLum: acá se llena tarde, en el primer draw con atlas, y si no hay atlas no se dibuja.
#define G(vm) ((void *)(MACHO_SLIDE + (vm)))
static uint32_t teensy_draw(uint8_t *self, uint32_t a1, uint32_t a2, uint32_t a3) {
    if (!*(uint32_t *)(self + 0x194)) {
        uint8_t *tex = *(uint8_t **)(self + 0xb4);
        uint8_t *atlas = tex ? *(uint8_t **)(tex + 0x84) : NULL;
        uint32_t n = atlas ? *(uint32_t *)(atlas + 0x28) : 0;
        static int logged;
        if (!logged++)
            l_info("patch: teensy UVs tarde: tex=%p atlas=%p n=%u", tex, atlas, (unsigned)n);
        if (!n)
            return 0;
        *(uint32_t *)(self + 0x18c) = n;
        call_guest(G(0x21f3d4), (uint32_t)(self + 0x190), 0, 0, 0);
        uint32_t dst = call_guest(G(0x336508), (uint32_t)(self + 0x190), n * 4, 0, 0);
        for (uint32_t i = 0; i < n; i++, dst += 0x20)
            call_guest(G(0xb1274), (uint32_t)atlas, i, dst, 0);
    }
    return call_guest(G(0x3b67d1), (uint32_t)self, a1, a2, a3);
}

void patch_init(void) {
    extern void ml_align_call(void);
    uint32_t *slot = (uint32_t *)G(0x56c7e8); // vtable TeensyUnlockManagerComponent, entrada del draw
    if (*slot == MACHO_SLIDE + 0x3b67cd)
        macho_write_word(slot, macho_thunk((uint32_t)teensy_draw, ml_align_call));
    else
        l_error("patch: vtable TeensyUnlockManager no coincide, sin parchear");

    // Ray_ScoreLumAIComponent::Update (0x2c1ff8) lee texture->uvAtlas->count sin mirar si el atlas
    // es NULL (la textura aún sin postCreate): data abort en 0x2c207c (log/dump 004-006). El resto
    // del motor (AtlasDynamicObject::addQuad) ya tolera atlas NULL; acá se agrega el mismo chequeo:
    //   str r1,[sp,#0x30]; cbz r0,1f; ldr.w r0,[r0,#0x84]; cbz r0,1f; ldr r0,[r0,#0x28]; str r0,[sp,#0x30]; 1:
    static const uint16_t lum[] = {0x910C, 0xB120, 0xF8D0, 0x0084, 0xB108, 0x6A80, 0x900C};
    if (macho_read_text(0x2c2070) == 0x28002101 && macho_read_text(0x2c2074) == 0xbf1e910c)
        macho_patch_text(0x2c2072, lum, sizeof(lum));
    else
        l_error("patch: ScoreLumAIComponent::Update no coincide, sin parchear");
}
