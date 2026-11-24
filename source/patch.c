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

// BezierPatch::drawPatch(this, BezierPatchParams *) en 0x1488c0 -> 0x148c98:
// 1) Cacheo estático: cada BezierPatch mantiene su VBO en GPU ([self+0x88], self+4). Si los 164 bytes
//    de params no cambiaron respecto al último draw del mismo (self, vb), saltamos el teselado y el
//    glBufferData y solo re-enviamos el mesh ya subido (0x5ab84), igual que el epílogo 0x148fbe..0x149008.
// 2) En miss, bajamos vdiv_used (+0xa0) sobre el mismo IB válido de la tabla 0x513ac0.
extern uint32_t objc_call_imp(void *imp, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                              uint32_t a6);
static uintptr_t bezier_orig;
static struct {
    uint8_t *self;
    void *vb;
    uint64_t hash;
} bez_cache[512];
static uint32_t bez_hits, bez_misses;

void patch_take_bez_stats(uint32_t *hits, uint32_t *misses) {
    *hits = bez_hits;
    *misses = bez_misses;
    bez_hits = bez_misses = 0;
}

static uint64_t hash_params(const uint32_t *p) {
    uint64_t h = 14695981039346656037ull;
    for (int i = 0; i < 41; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static void bezier_draw_hook(uint8_t *self, uint8_t *params) {
    if (!params) {
        call_guest((void *)bezier_orig, (uint32_t)self, 0, 0, 0);
        return;
    }
    uint32_t tex = *(uint32_t *)(params + 0x90);
    if (!tex)
        return;
    uint64_t h = hash_params((const uint32_t *)params);
    uint32_t slot = ((uintptr_t)self >> 4) & 511u;
    void *vb = *(void **)(self + 0x88);
    uint8_t *elem = *(uint8_t **)(self + 24);
    if (vb && elem && bez_cache[slot].self == self && bez_cache[slot].vb == vb && bez_cache[slot].hash == h) {
        *(uint32_t *)(elem + 16) = *(uint32_t *)(params + 0x88);
        *(uint32_t *)(self + 52) = *(uint32_t *)(params + 0x68);
        *(uint32_t *)(self + 56) = *(uint32_t *)(params + 0x6c);
        *(uint32_t *)(self + 60) = *(uint32_t *)(params + 0x70);
        *(uint32_t *)(self + 64) = *(uint32_t *)(params + 0x74);
        objc_call_imp(G(0x5ab85), *(uint32_t *)G(0x5937e0), (uint32_t)(self + 4), 10, 0,
                      *(uint32_t *)(params + 0x94), 0, *(uint32_t *)G(0x5442ec));
        bez_hits++;
        return;
    }
    uint32_t *ph = (uint32_t *)(params + 0x98);
    uint32_t *pv = (uint32_t *)(params + 0x9c);
    uint32_t *pu = (uint32_t *)(params + 0xa0);
    uint32_t old_h = *ph, old_v = *pv, old_u = *pu;
    uint32_t nh = old_h, nv = old_v, nu = old_u;
    if (old_h == 4 && old_v == 128) {
        uint32_t eff = old_u < old_v ? old_u : old_v;
        if (eff > 12) {
            nu = (eff + 3) >> 2;
            if (nu < 10)
                nu = 10;
            if (nu > 16)
                nu = 16;
        }
    } else if (old_h == 128 && old_v == 4) {
        nh = 8;
        nv = 2;
        nu = 2;
    } else if (old_h == 4 && old_v == 4 && old_u > 2) {
        nu = 2;
    }
    *ph = nh;
    *pv = nv;
    *pu = nu;
    call_guest((void *)bezier_orig, (uint32_t)self, (uint32_t)params, 0, 0);
    *ph = old_h;
    *pv = old_v;
    *pu = old_u;
    bez_cache[slot].self = self;
    bez_cache[slot].vb = *(void **)(self + 0x88);
    bez_cache[slot].hash = h;
    bez_misses++;
}

void patch_init(void) {
    extern void ml_align_call(void);
    uint32_t *slot = (uint32_t *)G(0x56c7e8); // vtable TeensyUnlockManagerComponent, entrada del draw
    if (*slot == MACHO_SLIDE + 0x3b67cd)
        macho_write_word(slot, macho_thunk((uint32_t)teensy_draw, ml_align_call));
    else
        l_error("patch: vtable TeensyUnlockManager no coincide, sin parchear");

    if (macho_read_text(0x1488c0) == 0xaf03b5f0u && macho_read_text(0x1488c4) == 0x8d04f84du) {
        bezier_orig = macho_hook_thumb8(0x1488c0, bezier_draw_hook);
        l_info("patch: BezierPatch LOD hook en 0x1488c0 -> tramp 0x%08X", (unsigned)bezier_orig);
    } else {
        l_error("patch: BezierPatch::drawPatch no coincide, sin parchear");
    }

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
