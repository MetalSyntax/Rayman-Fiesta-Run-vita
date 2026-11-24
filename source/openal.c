// OpenAL de iOS -> openal-soft de VitaSDK. Mismo ABI (softfp, enteros/punteros); solo hay que
// envolver lo que el juego obtiene por alcGetProcAddress (entra sin alinear la pila) y la extensión
// de Apple alBufferDataStatic (datos sin copiar -> acá se copian).
#include <AL/al.h>
#include <AL/alc.h>
#include <string.h>

#include "imports.h"
#include "log.h"
#include "macho.h"

extern void ml_align_call(void);

static void al_BufferDataStatic(ALuint buf, ALenum fmt, const ALvoid *data, ALsizei size, ALsizei freq) {
    alBufferData(buf, fmt, data, size, freq);
}
static void *al_GetProcAddress(ALCdevice *dev, const ALCchar *name) {
    void *fn = NULL;
    if (!strcmp(name, "alBufferDataStatic") || !strcmp(name, "alBufferDataStaticProc"))
        fn = (void *)al_BufferDataStatic;
    else
        fn = alcGetProcAddress(dev, name);
    l_info("alcGetProcAddress(%s) -> %s", name, fn ? "ok" : "NULL");
    return fn ? (void *)macho_thunk((uint32_t)fn, ml_align_call) : NULL;
}
static void *al_GetProcAddress2(const ALchar *name) { return al_GetProcAddress(NULL, name); }
static ALCdevice *al_OpenDevice(const ALCchar *name) {
    ALCdevice *d = alcOpenDevice(NULL); // el nombre de dispositivo de iOS no existe acá
    l_info("alcOpenDevice -> %p", d);
    return d;
}

extern void sfp_alSourcef(ALuint, ALenum, ALfloat), sfp_alSource3f(ALuint, ALenum, ALfloat, ALfloat, ALfloat);
extern void sfp_alListenerf(ALenum, ALfloat), sfp_alListener3f(ALenum, ALfloat, ALfloat, ALfloat);
extern void sfp_alBufferf(ALuint, ALenum, ALfloat), sfp_alDopplerFactor(ALfloat), sfp_alDopplerVelocity(ALfloat),
    sfp_alSpeedOfSound(ALfloat);
extern ALfloat sfp_alGetFloat(ALenum);

const import_entry openal_imports[] = {
    D(alBufferData), F(alBufferf, sfp_alBufferf), D(alBufferi), D(alDeleteBuffers), D(alDeleteSources), D(alGenBuffers),
    D(alGenSources), D(alGetError), D(alGetSourcei), D(alGetSourcef), D(alGetSourcefv), D(alGetSourceiv),
    D(alGetSource3f), D(alGetBufferi), D(alGetBufferf), F(alListener3f, sfp_alListener3f), D(alListenerfv),
    F(alListenerf, sfp_alListenerf), D(alListeneri), D(alGetListenerf), D(alGetListenerfv), D(alGetListener3f),
    F(alSource3f, sfp_alSource3f), D(alSource3i), D(alSourcefv), D(alSourceiv), D(alSourcePause), D(alSourcePlay),
    D(alSourceQueueBuffers), D(alSourceRewind), D(alSourceStop), D(alSourceUnqueueBuffers), F(alSourcef, sfp_alSourcef),
    D(alSourcei), D(alSourcePlayv), D(alSourceStopv), D(alSourcePausev), D(alSourceRewindv), D(alIsSource),
    D(alIsBuffer), D(alGetString), D(alGetInteger), F(alGetFloat, sfp_alGetFloat), D(alGetBoolean), D(alDistanceModel),
    F(alDopplerFactor, sfp_alDopplerFactor), F(alDopplerVelocity, sfp_alDopplerVelocity),
    F(alSpeedOfSound, sfp_alSpeedOfSound), D(alEnable), D(alDisable), D(alIsEnabled), D(alIsExtensionPresent),
    D(alGetEnumValue),
    D(alcCloseDevice), D(alcCreateContext), D(alcDestroyContext), D(alcGetContextsDevice), D(alcGetCurrentContext),
    D(alcMakeContextCurrent), D(alcProcessContext), D(alcSuspendContext), D(alcGetError), D(alcGetString),
    D(alcGetIntegerv), D(alcIsExtensionPresent), F(alcOpenDevice, al_OpenDevice),
    F(alcGetProcAddress, al_GetProcAddress), F(alGetProcAddress, al_GetProcAddress2),
};
TABLE_END(openal)
