// Puente softfp (aapcs) -> hardfp (aapcs-vfp) sin -Wl,--wrap (para no interceptar llamadas
// internas hardfp -> hardfp dentro de libvorbis, libopenal, libfreetype, libstdc++).
#include <stddef.h>

#define SFP __attribute__((pcs("aapcs")))
#define HFP __attribute__((pcs("aapcs-vfp")))

#define W_F1(fn) \
    extern float HFP fn(float); \
    float SFP sfp_##fn(float a) { return fn(a); }

#define W_F2(fn) \
    extern float HFP fn(float, float); \
    float SFP sfp_##fn(float a, float b) { return fn(a, b); }

#define W_D1(fn) \
    extern double HFP fn(double); \
    double SFP sfp_##fn(double a) { return fn(a); }

#define W_D2(fn) \
    extern double HFP fn(double, double); \
    double SFP sfp_##fn(double a, double b) { return fn(a, b); }

// libm (float)
W_F1(acosf) W_F1(asinf) W_F1(atanf) W_F2(atan2f) W_F1(cosf) W_F1(sinf) W_F1(tanf)
W_F1(expf) W_F1(exp2f) W_F1(logf) W_F1(log2f) W_F1(log10f) W_F2(powf) W_F1(sqrtf) W_F1(cbrtf)
W_F1(floorf) W_F1(ceilf) W_F1(fabsf) W_F2(fmodf) W_F1(roundf) W_F1(rintf) W_F1(truncf)
W_F1(coshf) W_F1(sinhf) W_F1(tanhf) W_F2(hypotf)

extern long HFP lrintf(float);
long SFP sfp_lrintf(float a) { return lrintf(a); }
extern long HFP lroundf(float);
long SFP sfp_lroundf(float a) { return lroundf(a); }
extern float HFP modff(float, float *);
float SFP sfp_modff(float a, float *p) { return modff(a, p); }

// libm (double)
W_D1(acos) W_D1(asin) W_D1(atan) W_D2(atan2) W_D1(cos) W_D1(sin) W_D1(tan)
W_D1(exp) W_D1(log) W_D1(log2) W_D1(log10) W_D2(pow) W_D1(sqrt)
W_D1(floor) W_D1(ceil) W_D1(fabs) W_D2(fmod) W_D1(round) W_D1(rint) W_D1(trunc)
W_D1(cosh) W_D1(sinh) W_D1(tanh) W_D2(hypot) W_D2(fmax) W_D2(fmin)

extern long HFP lrint(double);
long SFP sfp_lrint(double a) { return lrint(a); }
extern long long HFP llrint(double);
long long SFP sfp_llrint(double a) { return llrint(a); }
extern long HFP lround(double);
long SFP sfp_lround(double a) { return lround(a); }
extern double HFP ldexp(double, int);
double SFP sfp_ldexp(double a, int e) { return ldexp(a, e); }
extern double HFP frexp(double, int *);
double SFP sfp_frexp(double a, int *e) { return frexp(a, e); }
extern double HFP modf(double, double *);
double SFP sfp_modf(double a, double *p) { return modf(a, p); }

// libc (string -> float/double)
extern double HFP atof(const char *);
double SFP sfp_atof(const char *s) { return atof(s); }
extern double HFP strtod(const char *, char **);
double SFP sfp_strtod(const char *s, char **e) { return strtod(s, e); }
extern float HFP strtof(const char *, char **);
float SFP sfp_strtof(const char *s, char **e) { return strtof(s, e); }

// libmathneon (usado por vitaGL math_utils.c; nunca lo llaman libvorbis/libopenal)
extern void HFP __real_sincosf_c(float, float *);
void SFP __wrap_sincosf_c(float x, float *r) { __real_sincosf_c(x, r); }

// libopenal (funciones con argumentos o retorno float)
extern void HFP alSourcef(unsigned int, int, float);
void SFP sfp_alSourcef(unsigned int s, int p, float v) { alSourcef(s, p, v); }
extern void HFP alSource3f(unsigned int, int, float, float, float);
void SFP sfp_alSource3f(unsigned int s, int p, float a, float b, float c) { alSource3f(s, p, a, b, c); }
extern void HFP alListenerf(int, float);
void SFP sfp_alListenerf(int p, float v) { alListenerf(p, v); }
extern void HFP alListener3f(int, float, float, float);
void SFP sfp_alListener3f(int p, float a, float b, float c) { alListener3f(p, a, b, c); }
extern void HFP alBufferf(unsigned int, int, float);
void SFP sfp_alBufferf(unsigned int b, int p, float v) { alBufferf(b, p, v); }
extern void HFP alDopplerFactor(float);
void SFP sfp_alDopplerFactor(float v) { alDopplerFactor(v); }
extern void HFP alDopplerVelocity(float);
void SFP sfp_alDopplerVelocity(float v) { alDopplerVelocity(v); }
extern void HFP alSpeedOfSound(float);
void SFP sfp_alSpeedOfSound(float v) { alSpeedOfSound(v); }
extern float HFP alGetFloat(int);
float SFP sfp_alGetFloat(int p) { return alGetFloat(p); }

// libstdc++ (ostream::_M_insert<double>: en iOS ARMv7 el double va sin alinear en r1:r2)
extern void *HFP _ZNSo9_M_insertIdEERSoT_(void *, double);
void *SFP sfp_ostream_insert_d(void *os, unsigned int lo, unsigned int hi) {
    union { unsigned long long u; double d; } v = {((unsigned long long)hi << 32) | lo};
    return _ZNSo9_M_insertIdEERSoT_(os, v.d);
}
extern void *HFP _ZNSt13basic_ostreamIwSt11char_traitsIwEE9_M_insertIdEERS2_T_(void *, double);
void *SFP sfp_wostream_insert_d(void *os, unsigned int lo, unsigned int hi) {
    union { unsigned long long u; double d; } v = {((unsigned long long)hi << 32) | lo};
    return _ZNSt13basic_ostreamIwSt11char_traitsIwEE9_M_insertIdEERS2_T_(os, v.d);
}
