#pragma once
// Configuración propia de este juego (el resto de source/ es común a los ports iOS; se sincroniza
// con tools/sync_runtime.sh desde Ferrari-GT-Evolution-vita).
#define PORT_NAME "Rayman Fiesta Run"
#define PORT_SLUG "raymanfiesta"
#define PORT_EXEC "rfr_dev"            // CFBundleExecutable
// ux0: admite 8 carpetas como máximo y casa_gamedata llega a 9 bajo ux0:data/<slug>/game_app: sin "data/"
#define DATA_PATH "ux0:" PORT_SLUG "/"
#define PORT_ROTATION 0                // la GLKView del nib ya es apaisada (568x320): sin girar
#define PORT_STRETCH 0
#define PORT_SYSTEM_VERSION "6.1.3"    // iOS 6: evita los caminos de iOS 7 (GCController) que no hay
#define PORT_DEVICE_MACHINE "iPhone5,1" // iPhone 5: el juego elige GameViewController_iPhone5 (16:9 ≈ Vita)
#define GLES_POOL_MB 32
#define PORT_ACCEL_FROM_STICK 0
// iPhone 5: 320x568 puntos @2x. La GLKView dibuja a 1136x640 y gles_present lo baja a 960x544.
// ponytail: si el rendimiento no alcanza, PORT_GLK_SCALE 1.69f da 960x540 nativo (probar en consola).
#define PORT_SCREEN_W 320
#define PORT_SCREEN_H 568
#define PORT_SCREEN_SCALE 2.0f
#define PORT_GLK_SCALE 2.0f
