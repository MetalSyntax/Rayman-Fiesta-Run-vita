// AudioToolbox de iOS: AudioSession (stubs), AudioFile (lector de .ogg/.wav) y AudioQueue (salida PCM).
//
// La música de estos juegos es .m4a (AAC) leída con AudioFile y reproducida con AudioQueue. En la
// Vita no hay decodificador AAC a mano, así que tools/stage_data.sh convierte cada .m4a/.mp3/.caf
// a .ogg con el mismo nombre; AudioFileOpenURL abre el .ogg y lo presenta como LPCM 16 bits
// intercalado (1 paquete = 1 frame). El juego arma la AudioQueue con ese formato y acá un hilo por
// cola mezcla (remuestreo lineal a 48 kHz estéreo) hacia un puerto MAIN de sceAudioOut, y llama al
// callback del juego cuando un buffer se consumió.
// AudioFileStream (parser en memoria) no está: devuelve error y el juego queda sin esa música.
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vorbis/vorbisfile.h>

#include "foundation.h"
#include "hle.h"
#include "imports.h"
#include "log.h"
#include "macho.h"

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (b) << 16 | (c) << 8 | (d))
#define kAudioFormatLinearPCM FOURCC('l', 'p', 'c', 'm')
#define ERR_FILE_NOT_FOUND -43      // fnfErr
#define ERR_UNSUPPORTED FOURCC('t', 'y', 'p', '?')
#define ERR_EOF -39

typedef struct {
    double mSampleRate;
    uint32_t mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame, mChannelsPerFrame,
        mBitsPerChannel, mReserved;
} asbd;

// ---------------------------------------------------------------- AudioSession

static int as_ok(void) { return 0; }
static int as_GetProperty(uint32_t pid, uint32_t *size, void *data) {
    switch (pid) {
    case FOURCC('o', 't', 'h', 'r'): // OtherAudioIsPlaying
    case FOURCC('a', 'c', 'h', 'n'): // AudioInputAvailable (pre 3.0: 'aiav')
    case FOURCC('a', 'i', 'a', 'v'):
        *(uint32_t *)data = 0;
        *size = 4;
        return 0;
    case FOURCC('c', 'h', 's', 'r'): // CurrentHardwareSampleRate (Float64)
        *(double *)data = 44100.0;
        *size = 8;
        return 0;
    case FOURCC('c', 'h', 'o', 'c'): // CurrentHardwareOutputNumberChannels
        *(uint32_t *)data = 2;
        *size = 4;
        return 0;
    case FOURCC('c', 'h', 'o', 'v'): // CurrentHardwareOutputVolume (Float32)
        *(float *)data = 1.0f;
        *size = 4;
        return 0;
    case FOURCC('r', 'o', 'u', 't'): // AudioRoute (CFString)
        *(id *)data = ns_string_new("Speaker");
        *size = 4;
        return 0;
    }
    memset(data, 0, *size);
    return 0;
}

// ---------------------------------------------------------------- AudioFile (.ogg / .wav)

typedef struct {
    int kind; // 1 ogg, 2 wav
    OggVorbis_File vf;
    FILE *f;
    long data_off, data_len, pos_frames;
    int channels, rate;
    int64_t frames;
} audio_file;

static const char *try_ext(const char *in, const char *ext, char *out, int max) {
    snprintf(out, max, "%s", in);
    char *dot = strrchr(out, '.'), *sl = strrchr(out, '/');
    if (dot && (!sl || dot > sl))
        *dot = 0;
    strncat(out, ext, max - strlen(out) - 1);
    FILE *f = fopen(out, "rb");
    if (f)
        fclose(f);
    return f ? out : NULL;
}

static int wav_open(audio_file *a, const char *path) {
    FILE *f = fopen(path, "rb");
    uint8_t h[12];
    if (!f || fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        if (f)
            fclose(f);
        return -1;
    }
    uint8_t ch[8];
    int fmt_ok = 0;
    while (fread(ch, 1, 8, f) == 8) {
        uint32_t len = ch[4] | ch[5] << 8 | ch[6] << 16 | ch[7] << 24;
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fm[16];
            fread(fm, 1, 16, f);
            fmt_ok = (fm[0] | fm[1] << 8) == 1 && (fm[14] | fm[15] << 8) == 16;
            a->channels = fm[2] | fm[3] << 8;
            a->rate = fm[4] | fm[5] << 8 | fm[6] << 16 | fm[7] << 24;
            fseek(f, len - 16, SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            a->data_off = ftell(f);
            a->data_len = len;
            break;
        } else {
            fseek(f, (len + 1) & ~1, SEEK_CUR);
        }
    }
    if (!fmt_ok || !a->data_off) {
        fclose(f);
        return -1;
    }
    a->f = f;
    a->kind = 2;
    a->frames = a->data_len / (a->channels * 2);
    return 0;
}

// AudioFileOpenURL(CFURLRef url, perms, typeHint, AudioFileID *out)
static int af_OpenURL(id url, int perms, uint32_t hint, audio_file **out) {
    const char *in = ns_cstr((id)MSG0(url, "path"));
    char p[512];
    const char *path = try_ext(in, ".ogg", p, sizeof(p));
    audio_file *a = calloc(1, sizeof(*a));
    FILE *of = path ? fopen(path, "rb") : NULL;
    if (of)
        setvbuf(of, NULL, _IOFBF, 64 * 1024); // la música se lee mientras el juego carga: menos sceIoRead
    if (of && ov_open(of, &a->vf, NULL, 0)) // ov_open se adueña del FILE solo si tiene éxito
        fclose(of), of = NULL;
    if (of) {
        vorbis_info *vi = ov_info(&a->vf, -1);
        a->kind = 1;
        a->channels = vi->channels;
        a->rate = vi->rate;
        a->frames = ov_pcm_total(&a->vf, -1);
    } else if ((path = try_ext(in, ".wav", p, sizeof(p))) && !wav_open(a, path)) {
    } else if (!wav_open(a, in)) {
        path = in;
    } else {
        l_error("AudioFileOpenURL(%s): sin .ogg/.wav convertido (tools/stage_data.sh)", in);
        free(a);
        *out = NULL;
        return ERR_FILE_NOT_FOUND;
    }
    l_info("AudioFile %s: %d Hz, %d canales, %lld frames", path, a->rate, a->channels, (long long)a->frames);
    *out = a;
    return 0;
}
// Para ExtAudioFile (ios5.c)
static int af_OpenURL(id url, int perms, uint32_t hint, audio_file **out);
int af_open_path(const char *path, void **out) {
    return af_OpenURL(MSG1(objc_getClass("NSURL"), "fileURLWithPath:", ns_str(path)), 1, 0, (audio_file **)out);
}
static void fill_asbd(audio_file *a, asbd *d);
int af_format(void *a, asbd *d, int64_t *frames) {
    fill_asbd(a, d);
    *frames = ((audio_file *)a)->frames;
    return 0;
}
static long read_frames(audio_file *a, int64_t start, long nframes, int16_t *out);
long af_read_frames(void *a, int64_t start, long n, int16_t *out) { return read_frames(a, start, n, out); }
int af_close(void *a);

static int af_Close(audio_file *a) {
    if (!a)
        return 0;
    if (a->kind == 1)
        ov_clear(&a->vf);
    else if (a->f)
        fclose(a->f);
    free(a);
    return 0;
}
static void fill_asbd(audio_file *a, asbd *d) {
    *d = (asbd){a->rate, kAudioFormatLinearPCM, 0xC /* signed int | packed */, 2 * a->channels, 1,
                2 * a->channels, a->channels, 16, 0};
}
static int af_GetPropertyInfo(audio_file *a, uint32_t pid, uint32_t *size, uint32_t *writable) {
    if (size)
        *size = pid == FOURCC('d', 'f', 'm', 't') ? sizeof(asbd) : pid == FOURCC('m', 'g', 'i', 'c') ? 0 : 8;
    if (writable)
        *writable = 0;
    return pid == FOURCC('m', 'g', 'i', 'c') ? ERR_UNSUPPORTED : 0;
}
static int af_GetProperty(audio_file *a, uint32_t pid, uint32_t *size, void *data) {
    if (!a)
        return -50;
    switch (pid) {
    case FOURCC('d', 'f', 'm', 't'): // DataFormat
        fill_asbd(a, data);
        *size = sizeof(asbd);
        return 0;
    case FOURCC('p', 'c', 'n', 't'): // AudioDataPacketCount (UInt64)
        *(uint64_t *)data = a->frames;
        *size = 8;
        return 0;
    case FOURCC('b', 'c', 'n', 't'): // AudioDataByteCount (UInt64)
        *(uint64_t *)data = a->frames * 2 * a->channels;
        *size = 8;
        return 0;
    case FOURCC('p', 's', 'z', 'e'): // MaximumPacketSize (UInt32)
    case FOURCC('p', 'k', 'u', 'b'): // PacketSizeUpperBound
        *(uint32_t *)data = 2 * a->channels;
        *size = 4;
        return 0;
    case FOURCC('e', 'd', 'u', 'r'): // EstimatedDuration (Float64)
        *(double *)data = (double)a->frames / a->rate;
        *size = 8;
        return 0;
    case FOURCC('f', 'f', 'm', 't'): // FileFormat
        *(uint32_t *)data = FOURCC('W', 'A', 'V', 'E');
        *size = 4;
        return 0;
    }
    l_info("AudioFileGetProperty('%c%c%c%c') sin soporte", pid >> 24, pid >> 16, pid >> 8, pid);
    return ERR_UNSUPPORTED;
}
static long read_frames(audio_file *a, int64_t start, long nframes, int16_t *out) {
    if (start >= a->frames)
        return 0;
    if (start + nframes > a->frames)
        nframes = a->frames - start;
    long got = 0;
    if (a->kind == 1) {
        if (a->pos_frames != start)
            ov_pcm_seek(&a->vf, start);
        int bs;
        while (got < nframes) {
            long r = ov_read(&a->vf, (char *)(out + got * a->channels), (nframes - got) * 2 * a->channels, 0, 2, 1, &bs);
            if (r <= 0)
                break;
            got += r / (2 * a->channels);
        }
    } else {
        fseek(a->f, a->data_off + start * 2 * a->channels, SEEK_SET);
        got = fread(out, 2 * a->channels, nframes, a->f);
    }
    a->pos_frames = start + got;
    return got;
}
// AudioFileReadPackets(file, useCache, UInt32 *outBytes, descs, SInt64 startPacket, UInt32 *ioNum, void *buf)
// SInt64 en el 5to argumento: Darwin lo pasa en la pila sin alinear (2 palabras seguidas).
static int af_ReadPackets(audio_file *a, int cache, uint32_t *out_bytes, void *descs, uint32_t start_lo,
                          uint32_t start_hi, uint32_t *io_num, void *buf) {
    if (!a)
        return -50;
    int64_t start = (int64_t)start_hi << 32 | start_lo;
    long got = read_frames(a, start, *io_num, buf);
    *io_num = got;
    if (out_bytes)
        *out_bytes = got * 2 * a->channels;
    return 0;
}
// AudioFileReadBytes(file, useCache, SInt64 startByte, UInt32 *ioNumBytes, void *buf): startByte en r2:r3
static int af_ReadBytes(audio_file *a, int cache, uint32_t start_lo, uint32_t start_hi, uint32_t *io_bytes, void *buf) {
    if (!a)
        return -50;
    int fb = 2 * a->channels;
    int64_t start = ((int64_t)start_hi << 32 | start_lo) / fb;
    long got = read_frames(a, start, *io_bytes / fb, buf);
    *io_bytes = got * fb;
    return got ? 0 : ERR_EOF;
}
static int af_StreamOpen(void *ud, void *prop_cb, void *pkt_cb, uint32_t hint, void **out) {
    l_error("AudioFileStreamOpen: sin soporte (música por streaming en memoria)");
    if (out)
        *out = NULL;
    return ERR_UNSUPPORTED;
}
static int af_StreamFail(void) { return ERR_UNSUPPORTED; }

// ---------------------------------------------------------------- AudioQueue (salida PCM)

typedef struct {
    uint32_t mAudioDataBytesCapacity;
    void *mAudioData;
    uint32_t mAudioDataByteSize;
    void *mUserData;
    uint32_t mPacketDescriptionCapacity;
    void *mPacketDescriptions;
    uint32_t mPacketDescriptionCount;
} aq_buffer;

#define MAX_QBUF 16
#define OUT_RATE 48000
#define GRAIN 1024
typedef struct {
    asbd fmt;
    void *cb, *ud;
    aq_buffer *fifo[MAX_QBUF];
    int head, count;
    volatile int running, alive;
    float volume;
    SceUID thread, lock;
    int port;
    void *listener, *listener_ud; // kAudioQueueProperty_IsRunning
    double frac;                  // posición fraccional del remuestreo
    int pos;                      // frame dentro del buffer actual
} audio_queue;

static void aq_lock(audio_queue *q) { sceKernelLockMutex(q->lock, 1, NULL); }
static void aq_unlock(audio_queue *q) { sceKernelUnlockMutex(q->lock, 1); }

static void notify_running(audio_queue *q) {
    if (q->listener)
        call_guest(q->listener, (uint32_t)q->listener_ud, (uint32_t)q, FOURCC('a', 'q', 'r', 'n'), 0);
}

static int aq_thread(SceSize args, void *argp) {
    audio_queue *q = *(audio_queue **)argp;
    int16_t out[GRAIN * 2];
    int ch = q->fmt.mChannelsPerFrame ? q->fmt.mChannelsPerFrame : 2;
    double step = q->fmt.mSampleRate / OUT_RATE;
    while (q->alive) {
        if (!q->running) {
            sceKernelDelayThread(5000);
            continue;
        }
        int n = 0;
        while (n < GRAIN) {
            aq_lock(q);
            aq_buffer *b = q->count ? q->fifo[q->head] : NULL;
            aq_unlock(q);
            if (!b) {
                memset(out + n * 2, 0, (GRAIN - n) * 4); // sin datos: silencio
                break;
            }
            const int16_t *s = b->mAudioData;
            int frames = b->mAudioDataByteSize / (2 * ch);
            while (n < GRAIN && q->pos < frames) {
                int i = q->pos;
                int16_t l = s[i * ch], r = ch > 1 ? s[i * ch + 1] : l;
                out[n * 2] = l * q->volume;
                out[n * 2 + 1] = r * q->volume;
                n++;
                q->frac += step;
                while (q->frac >= 1.0) {
                    q->frac -= 1.0;
                    q->pos++;
                }
            }
            if (q->pos >= frames) { // buffer consumido: devolverlo al juego
                aq_lock(q);
                q->head = (q->head + 1) % MAX_QBUF;
                q->count--;
                aq_unlock(q);
                q->pos = 0;
                call_guest(q->cb, (uint32_t)q->ud, (uint32_t)q, (uint32_t)b, 0);
            }
        }
        sceAudioOutOutput(q->port, out);
    }
    return 0;
}

// AudioQueueNewOutput(const asbd *fmt, cb, ud, CFRunLoopRef, CFStringRef mode, UInt32 flags, AudioQueueRef *out)
static int aq_NewOutput(const asbd *fmt, void *cb, void *ud, void *rl, void *mode, uint32_t flags, audio_queue **out) {
    if (fmt->mFormatID != kAudioFormatLinearPCM || fmt->mBitsPerChannel != 16) {
        l_error("AudioQueueNewOutput: formato '%c%c%c%c' %u bits sin soporte", fmt->mFormatID >> 24,
                fmt->mFormatID >> 16, fmt->mFormatID >> 8, fmt->mFormatID, (unsigned)fmt->mBitsPerChannel);
        *out = NULL;
        return ERR_UNSUPPORTED;
    }
    audio_queue *q = calloc(1, sizeof(*q));
    q->fmt = *fmt;
    q->cb = cb;
    q->ud = ud;
    q->volume = 1.0f;
    q->alive = 1;
    q->lock = sceKernelCreateMutex("ml_aq", 0, 0, NULL);
    q->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, GRAIN, OUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (q->port < 0) {
        l_error("AudioQueue: sceAudioOutOpenPort = 0x%08X", q->port);
        free(q);
        *out = NULL;
        return -1;
    }
    q->thread = sceKernelCreateThread("ml_aq", aq_thread, 0x10000100, 0x10000, 0, 0, NULL);
    sceKernelStartThread(q->thread, sizeof(q), &q);
    l_info("AudioQueue: %.0f Hz, %u canales", fmt->mSampleRate, (unsigned)fmt->mChannelsPerFrame);
    *out = q;
    return 0;
}
static int aq_AllocateBuffer(audio_queue *q, uint32_t size, aq_buffer **out) {
    aq_buffer *b = calloc(1, sizeof(*b));
    b->mAudioDataBytesCapacity = size;
    b->mAudioData = calloc(1, size);
    *out = b;
    return 0;
}
static int aq_AllocateBufferPD(audio_queue *q, uint32_t size, uint32_t npd, aq_buffer **out) {
    aq_AllocateBuffer(q, size, out);
    (*out)->mPacketDescriptionCapacity = npd;
    (*out)->mPacketDescriptions = calloc(npd ? npd : 1, 16);
    return 0;
}
static int aq_FreeBuffer(audio_queue *q, aq_buffer *b) {
    if (b) {
        free(b->mAudioData);
        free(b->mPacketDescriptions);
        free(b);
    }
    return 0;
}
static int aq_Enqueue(audio_queue *q, aq_buffer *b, uint32_t npd, void *pd) {
    if (!q)
        return -50;
    aq_lock(q);
    int r = 0;
    if (q->count < MAX_QBUF) {
        q->fifo[(q->head + q->count) % MAX_QBUF] = b;
        q->count++;
    } else {
        r = -1;
    }
    aq_unlock(q);
    return r;
}
static int aq_Start(audio_queue *q, void *time) {
    if (!q)
        return -50;
    q->running = 1;
    notify_running(q);
    return 0;
}
static int aq_Stop(audio_queue *q, int immediate) {
    if (!q)
        return -50;
    q->running = 0;
    aq_lock(q);
    q->count = 0;
    q->pos = 0;
    aq_unlock(q);
    notify_running(q);
    return 0;
}
static int aq_Pause(audio_queue *q) {
    if (q)
        q->running = 0;
    return 0;
}
static int aq_Dispose(audio_queue *q, int immediate) {
    if (!q)
        return 0;
    q->running = 0;
    q->alive = 0;
    sceKernelWaitThreadEnd(q->thread, NULL, NULL);
    sceKernelDeleteThread(q->thread);
    sceAudioOutReleasePort(q->port);
    sceKernelDeleteMutex(q->lock);
    free(q);
    return 0;
}
// AudioQueueSetParameter(q, id, Float32 value): float en r2 (softfp)
static int aq_SetParameter(audio_queue *q, uint32_t pid, float v) {
    if (q && pid == 1) // kAudioQueueParam_Volume
        q->volume = v < 0 ? 0 : v > 1 ? 1 : v;
    return 0;
}
static int aq_GetParameter(audio_queue *q, uint32_t pid, float *v) {
    *v = q && pid == 1 ? q->volume : 0;
    return 0;
}
static int aq_GetProperty(audio_queue *q, uint32_t pid, void *data, uint32_t *size) {
    if (pid == FOURCC('a', 'q', 'r', 'n')) { // IsRunning
        *(uint32_t *)data = q && q->running;
        *size = 4;
        return 0;
    }
    if (pid == FOURCC('a', 'q', 'f', 't')) { // StreamDescription
        memcpy(data, &q->fmt, sizeof(asbd));
        *size = sizeof(asbd);
        return 0;
    }
    return ERR_UNSUPPORTED;
}
static int aq_AddListener(audio_queue *q, uint32_t pid, void *fn, void *ud) {
    if (q && pid == FOURCC('a', 'q', 'r', 'n'))
        q->listener = fn, q->listener_ud = ud;
    return 0;
}
static int aq_ok(void) { return 0; }

int af_close(void *a) { return af_Close(a); }

const import_entry audio_imports[] = {
    F(AudioSessionInitialize, as_ok), F(AudioSessionSetActive, as_ok), F(AudioSessionSetProperty, as_ok),
    F(AudioSessionGetProperty, as_GetProperty), F(AudioSessionAddPropertyListener, as_ok),
    F(AudioSessionRemovePropertyListener, as_ok), F(AudioSessionGetPropertySize, as_ok),
    F(AudioServicesPlayAlertSound, as_ok), F(AudioServicesPlaySystemSound, as_ok),
    F(AudioServicesCreateSystemSoundID, as_ok), F(AudioServicesDisposeSystemSoundID, as_ok),
    F(AudioFileOpenURL, af_OpenURL), F(AudioFileClose, af_Close), F(AudioFileGetProperty, af_GetProperty),
    F(AudioFileGetPropertyInfo, af_GetPropertyInfo), F(AudioFileReadPackets, af_ReadPackets),
    F(AudioFileReadPacketData, af_ReadPackets), F(AudioFileReadBytes, af_ReadBytes),
    F(AudioFileStreamOpen, af_StreamOpen), F(AudioFileStreamParseBytes, af_StreamFail),
    F(AudioFileStreamGetProperty, af_StreamFail), F(AudioFileStreamClose, aq_ok),
    F(AudioQueueNewOutput, aq_NewOutput), F(AudioQueueAllocateBuffer, aq_AllocateBuffer),
    F(AudioQueueAllocateBufferWithPacketDescriptions, aq_AllocateBufferPD), F(AudioQueueFreeBuffer, aq_FreeBuffer),
    F(AudioQueueEnqueueBuffer, aq_Enqueue), F(AudioQueueStart, aq_Start), F(AudioQueueStop, aq_Stop),
    F(AudioQueuePause, aq_Pause), F(AudioQueueDispose, aq_Dispose), F(AudioQueueSetParameter, aq_SetParameter),
    F(AudioQueueGetParameter, aq_GetParameter), F(AudioQueueGetProperty, aq_GetProperty),
    F(AudioQueueSetProperty, aq_ok), F(AudioQueueAddPropertyListener, aq_AddListener),
    F(AudioQueueRemovePropertyListener, aq_ok), F(AudioQueuePrime, aq_ok), F(AudioQueueFlush, aq_ok),
    F(AudioQueueReset, aq_ok),
};
TABLE_END(audio)
