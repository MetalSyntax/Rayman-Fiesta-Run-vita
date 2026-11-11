# Rayman Fiesta Run — Progreso del port (iOS → PS Vita)

## 2026-11-04 — Inicialización y runtime completo (todo offline; falta solo probar en consola)

### Hecho
- `psvita-toolkit init --ipa "IPA/Rayman_Fiesta_Run_1.2.3.ipa" --name "Rayman Fiesta Run" --titleid PSVRFR001 --slug raymanfiesta --vita-ip 192.168.3.15` (commit del scaffold).
- Binario `rfr_dev`: armv7/armv7s PIE (Thumb-2), se usa armv7, cryptid 0 (descifrado). MinimumOSVersion 5.0.
- Motor: Pasta Games "ITF"/UbiArt-lite: la misma familia de motor que Rayman-Jungle-Run-vita (Android, soloader, ya jugable) → referencia de ciclo de vida y assets (`casa_gamedata/`). GLES1 (+ algunas llamadas GLES2), sqlite, GameKit/StoreKit/Social (stubs), PLCrashReporter (stubs Mach).
- Arranque: Sin MainWindow: `main` (encontrado desde `start`, binario sin símbolos) → `UIApplicationMain` → delegate crea `GameViewController` (subclase de **GLKViewController**) con `initWithNibName:@"GameViewController_iPhone5"` → nib con **GLKView** (precompilado en `nibs/`).
- **El boilerplate iOS que copia el toolkit no compila ni tiene runtime ObjC** → se reemplazó por el loader probado de Inotia2-vita + un runtime nuevo y común a los 6 ports iOS (copia maestra en `Ferrari-GT-Evolution-vita/source`, se sincroniza con `tools/sync_runtime.sh <port>`). Detalle en `PORTING_PLAN.md` §1.
- `tools/ios_prep.py` → `rebase.bin` **62150** palabras, `binds.bin` **6388** slots / 862 símbolos. Modo: PIE/dyld_info (exacto); 10 binds resueltos dentro de la imagen.
  - El clasificador se validó contra HAWX (que trae símbolos): 0 punteros perdidos, 0 falsos positivos en datos.
- Cobertura de imports: todos resuelven (implementación, clase vacía autogenerada, NSString constante sintetizada o stub `unimpl` que lo registra en el log).
- C++: `tools/gen_cxx.py` mapea la libstdc++ de iOS (ABI COW) a la de VitaSDK (`source/cxx_imports.c`).
- Audio: OpenAL + `ExtAudioFile` (wav/mp3 → `.ogg`); `AVAudioPlayer` (clase vacía).
- Video: Sin videos.
- Controles: Runner de 1-2 botones táctiles (saltar/golpear) → `controls.txt`.
- Rotación del drawable: 0 (la GLKView del nib ya es 568x320 apaisada).
- Build OK: `psvita-toolkit build --preset debug` → `build/raymanfiesta.vpk`.
- Datos preparados: `tools/stage_data.sh` → `ux0_data/raymanfiesta/` (bundle, `rebase.bin`, `binds.bin`, `controls.txt`, música `.ogg`).

### Para probar en consola (Vita 192.168.3.15, VitaShell FTP en 1337)
1. Instalar `build/raymanfiesta.vpk`.
2. `python3 -I tools/deploy_data.py` (sube `ux0_data/raymanfiesta/` → `ux0:data/raymanfiesta/`).
3. Requisitos en la consola: `kubridge.skprx` en `ur0:tai/config.txt` y `libshacccg.suprx` (ShaRKBR33D).
4. Abrir el juego; si se cierra o queda en negro: `python3 -I tools/deploy_data.py --logs` y leer `logs/raymanfiesta_NNN.log` (+ `.psp2dmp`).
5. Rotación incorrecta: mantener SELECT+START 2 s (cicla y guarda `rotation.txt`).
6. Botones: editar `ux0:data/raymanfiesta/controls.txt` por FTP (coordenadas de lo que se ve), sin recompilar.

### Riesgos conocidos
- GLKView a 1136x640 (iPhone 5 @2x): si no rinde, `PORT_GLK_SCALE 1.69f` (960x540 nativo).
- Bloques/GCD/ARC implementados por primera vez (sin prueba previa en ningún port).
- Assets: 1,1 GB.

### Revisión independiente del runtime (misma sesión, antes de la consola)
Un revisor aparte leyó asm.S/objc.c/macho.c. Se corrigió:
- **Crítico:** las cadenas ObjC (selectores, nombres de clase, UTF-16) viven en `__TEXT` y se leían antes de `macho_finalize()` (el bloque RX todavía estaba vacío). Ahora `foundation_fix_cfstrings` + `objc_init_image` corren después; las escrituras en `__TEXT` van por `macho_write_word` (kubridge).
- Pool de autorelease por hilo (TLS); antes era uno solo compartido → use-after-free con hilos/GCD.
- Caché de métodos sin carrera (cls=0 → imp/sel → cls, con barreras y re-chequeo).
- `-dealloc` no puede correr dos veces (rc centinela durante el dealloc).
- `methodForSelector:` devuelve un thunk con `ml_align_call` para IMPs nativas; `objc_ivar_set` libera el valor anterior.
- Pendiente (latente, sin uso en estos binarios): `+initialize`/`+load` no se envían; `nil_imp` en llamadas `_stret` deja el struct sin tocar.
- Verificado correcto por el revisor: balance de pila/registros de todos los trampolines, `ml_dispatch_imp` (Thumb incluido), `objc_msgSendSuper2`, setjmp, cadena de metaclases, mapeo y thunks de macho.c, firmas nativas con CGRect/NSRange/double.

## 2026-11-11 — Corrida 012: crash en el mapa del mundo (TeensyUnlockManager)
- Dump: `Undefined instruction` en PC 0x983b6b6a → Mach-O 0x3b6b6a = `trap` de chequeo de rango en el draw
  (0x3b67d0) de `Ray_RFR_TeensyUnlockManagerComponent` (RTTI de la vtable 0x56c7b8). Tabla de UVs vacía
  (+0x190/+0x194 = 0, +0x18c basura): el init (0x3b65ac) la llena solo si `texture(+0xb4)->uvAtlas(+0x84)`
  ya existe, y la textura (`teensy_icone.png`, atlas presente en el IPA) aún no pasó por postCreate. Mismo caso que ScoreLum.
- Parche (`source/patch.c`): la entrada del draw en la vtable (0x56c7e8) pasa por `teensy_draw`, que llena
  la tabla tarde (0x21f3d4 clear, 0x336508 resize, 0xb1274 por frame) o no dibuja si sigue sin atlas.
  Log de control: `patch: teensy UVs tarde: tex=… atlas=… n=…` (una vez).

## 2026-11-11 — Corrida 013: jugable; fluidez ~10 FPS reales con contador a 30/60
- Parche del teensy OK (sin crash): `tex=0x87fc9aa8 atlas=0x0 n=0` → esa textura sigue sin atlas al dibujar,
  el contador de teensies no se dibuja (pendiente, menor).
- Nivel: 600 presents / 10 s pero ~20 draw calls por present → la mayoría de los swaps repiten el cuadro.
  El juego dibuja y presenta solo desde `drawFrame` (CADisplayLink, 60 Hz → `presentRenderbuffer:`) y su
  `glkView:drawInRect:` está vacío (`bx lr` en 0xbbd0), pero nuestro GLKViewController (timer 30 Hz) hacía
  otro `gles_present` con vsync en cada tick: vblanks perdidos y cuadros repetidos.
- Cambio (`source/uikit.c`, pendiente llevar a la copia maestra): `gv_display` omite el swap si el
  drawInRect no dibujó nada y el juego ya presentó por su cuenta. La línea `vivo:` ahora cuenta presents
  del juego y swaps de GLKView omitidos.

## 2026-11-11 — Corrida 014: velocidad OK; la partida guardada no carga
- En la Vita `Documents/` tiene `slot0.dat` (2393 B), `Activities`, `Achievements`: el juego escribe.
- El slot del juego es un `ByteArrayDocument : UIDocument` (`initWithFileName:url:`, `saveToURL:…`,
  `openWithCompletionHandler:`, `loadFromContents:ofType:error:`). `UIDocument` era una clase vacía →
  `-[UIDocument initWithFileURL:] sin implementar` → nil → open/save del documento no hacían nada.
  `Activities` se lee con `NSFileHandle`, que también faltaba ("No data in file Activities").
- Cambios (runtime, pendiente llevar a la copia maestra): `UIDocument` local en `uikit.c` (open/revert →
  `loadFromContents:`, save → `contentsForType:` + `writeToFile:`, completions ^(BOOL) en la cola
  principal vía `gcd_main_block` de `ios5.c`); `NSFileHandle` sobre `FILE*` en `foundation.c`.
  Log de control: `UIDocument open/save <ruta> -> 0/1`.
