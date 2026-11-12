// Touch frontal + botones de la Vita -> toques de UIKit (uikit.c) en coordenadas de la vista.
//
// La pantalla muestra el drawable girado/escalado (gles_present); acá se hace el mapeo inverso:
// punto de pantalla -> normalizado en el rectángulo destino -> coordenadas de la vista de iOS.
// Botones: cada uno "toca" un punto configurable en ux0:data/<slug>/controls.txt, en coordenadas
// lógicas de lo que se ve (p. ej. 480x320 para un juego apaisado):
//     CROSS 420 260
//     RTRIGGER 440 160
// Se edita por FTP sin recompilar. SELECT+START (2 s) cambia la rotación y la guarda.
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <string.h>

#include "gles.h"
#include "hle.h"
#include "input.h"
#include "log.h"

#define FINGERS 6
#define BTN_FINGER0 2 // los botones usan dedos virtuales 2..7 (los reales, 0..1)
extern int port_rotation;

static struct {
    int id, down, moving;
    float x0, y0; // donde apoyó (puntos)
} fingers[2];
#define TOUCH_SLOP 4.0f // pt: el panel tiembla; iOS no manda touchesMoved sin movimiento y el mapa lo toma como arrastre
static uint32_t prev_buttons;
static float stick_x, stick_y;
static uint64_t combo_since;

static const struct {
    const char *name;
    uint32_t mask;
} names[] = {
    {"CROSS", SCE_CTRL_CROSS},       {"CIRCLE", SCE_CTRL_CIRCLE}, {"SQUARE", SCE_CTRL_SQUARE},
    {"TRIANGLE", SCE_CTRL_TRIANGLE}, {"LTRIGGER", SCE_CTRL_LTRIGGER}, {"RTRIGGER", SCE_CTRL_RTRIGGER},
    {"START", SCE_CTRL_START},       {"SELECT", SCE_CTRL_SELECT}, {"UP", SCE_CTRL_UP},
    {"DOWN", SCE_CTRL_DOWN},         {"LEFT", SCE_CTRL_LEFT},     {"RIGHT", SCE_CTRL_RIGHT},
};
#define MAX_BIND 6
static struct {
    uint32_t mask;
    float x, y; // lógicas (lo que se ve)
} binds[MAX_BIND];
static int nbinds;

static void load_controls(void) {
    FILE *f = fopen(DATA_PATH "controls.txt", "r");
    if (!f) {
        l_info("sin controls.txt: solo touch + stick (acelerómetro)");
        return;
    }
    char line[96], n[32];
    float x, y;
    while (fgets(line, sizeof(line), f) && nbinds < MAX_BIND)
        if (line[0] != '#' && sscanf(line, "%31s %f %f", n, &x, &y) == 3)
            for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
                if (!strcmp(n, names[i].name))
                    binds[nbinds++] = (typeof(binds[0])){names[i].mask, x, y};
    fclose(f);
    l_info("controls.txt: %d botones", nbinds);
}

void input_init(void) {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    FILE *f = fopen(DATA_PATH "rotation.txt", "r");
    if (f) {
        int r;
        if (fscanf(f, "%d", &r) == 1 && r >= 0 && r <= 2)
            port_rotation = r;
        fclose(f);
    }
    load_controls();
}

void input_stick(float *x, float *y) {
    *x = stick_x;
    *y = stick_y;
}

// Normalizado [0,1] en lo que se ve -> coordenadas de la vista en puntos (ver uv_rot en gles.c).
static void to_view(float nx, float ny, float *vx, float *vy) {
    float pw = fb_w / fb_scale, ph = fb_h / fb_scale; // UIKit entrega toques en puntos, no en píxeles
    switch (gles_rotation()) {
    case 1:
        *vx = (1 - ny) * pw, *vy = nx * ph;
        break;
    case 2:
        *vx = ny * pw, *vy = (1 - nx) * ph;
        break;
    default:
        *vx = nx * pw, *vy = ny * ph;
    }
}

static int screen_to_view(int sx, int sy, float *vx, float *vy) {
    int dx, dy, dw, dh;
    gles_dest_rect(&dx, &dy, &dw, &dh);
    float nx = (sx - dx) / (float)dw, ny = (sy - dy) / (float)dh;
    if (nx < 0 || nx > 1 || ny < 0 || ny > 1)
        return 0;
    to_view(nx, ny, vx, vy);
    return 1;
}

static void logical_size(float *w, float *h) {
    *w = gles_rotation() ? fb_h : fb_w;
    *h = gles_rotation() ? fb_w : fb_h;
}

void input_poll(void) {
    SceTouchData t;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) < 1)
        t.reportNum = 0;
    // dos dedos como máximo (los juegos de 2009 rara vez usan más)
    for (int k = 0; k < 2; k++) {
        int found = -1;
        for (int i = 0; i < (int)t.reportNum; i++)
            if (fingers[k].down && t.report[i].id == fingers[k].id)
                found = i;
        if (fingers[k].down && found < 0) {
            fingers[k].down = 0;
            ui_touch_event(k, 3, -1, -1);
        }
    }
    for (int i = 0; i < (int)t.reportNum && i < 2; i++) {
        float vx, vy;
        if (!screen_to_view(t.report[i].x / 2, t.report[i].y / 2, &vx, &vy))
            continue;
        int k = -1;
        for (int j = 0; j < 2; j++)
            if (fingers[j].down && fingers[j].id == t.report[i].id)
                k = j;
        if (k < 0) {
            for (int j = 0; j < 2 && k < 0; j++)
                if (!fingers[j].down)
                    k = j;
            if (k < 0)
                continue;
            fingers[k].down = 1;
            fingers[k].id = t.report[i].id;
            fingers[k].moving = 0;
            fingers[k].x0 = vx, fingers[k].y0 = vy;
            ui_touch_event(k, 0, vx, vy);
        } else {
            float dx = vx - fingers[k].x0, dy = vy - fingers[k].y0;
            if (!fingers[k].moving && dx * dx + dy * dy > TOUCH_SLOP * TOUCH_SLOP)
                fingers[k].moving = 1;
            if (fingers[k].moving)
                ui_touch_event(k, 1, vx, vy);
        }
    }

    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    stick_x = (pad.lx - 128) / 127.0f;
    stick_y = (pad.ly - 128) / 127.0f;
    if (stick_x > -0.12f && stick_x < 0.12f)
        stick_x = 0;
    if (stick_y > -0.12f && stick_y < 0.12f)
        stick_y = 0;
    uint32_t b = pad.buttons, changed = b ^ prev_buttons;
    for (int i = 0; i < nbinds; i++) {
        if (!(changed & binds[i].mask))
            continue;
        float lw, lh, vx, vy;
        logical_size(&lw, &lh);
        to_view(binds[i].x / lw, binds[i].y / lh, &vx, &vy);
        ui_touch_event(BTN_FINGER0 + i, (b & binds[i].mask) ? 0 : 3, vx, vy);
    }
    // SELECT+START sostenidos 2 s: rotación siguiente (se guarda).
    uint64_t now = sceKernelGetProcessTimeWide();
    if ((b & (SCE_CTRL_SELECT | SCE_CTRL_START)) == (SCE_CTRL_SELECT | SCE_CTRL_START)) {
        if (!combo_since)
            combo_since = now;
        else if (now - combo_since > 2000000) {
            port_rotation = (port_rotation + 1) % 3;
            combo_since = now + 60000000; // una vez por pulsación
            FILE *f = fopen(DATA_PATH "rotation.txt", "w");
            if (f) {
                fprintf(f, "%d\n", port_rotation);
                fclose(f);
            }
            l_info("rotación -> %d", port_rotation);
        }
    } else {
        combo_since = 0;
    }
    prev_buttons = b;
}
