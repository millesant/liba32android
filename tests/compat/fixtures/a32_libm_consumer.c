// Freestanding ARMv7/Android consumer for the evidence-backed partial libm.so.
// Imports the complete 66-symbol math surface selected from supplied VLC.

__attribute__((visibility("default"))) double acos(double);
__attribute__((visibility("default"))) double asin(double);
__attribute__((visibility("default"))) double atan2(double, double);
__attribute__((visibility("default"))) double cos(double);
__attribute__((visibility("default"))) float cosf(float);
__attribute__((visibility("default"))) double exp(double);
__attribute__((visibility("default"))) double floor(double);
__attribute__((visibility("default"))) double frexp(double, int*);
__attribute__((visibility("default"))) double ldexp(double, int);
__attribute__((visibility("default"))) double log(double);
__attribute__((visibility("default"))) double log10(double);
__attribute__((visibility("default"))) float log10f(float);
__attribute__((visibility("default"))) double pow(double, double);
__attribute__((visibility("default"))) float powf(float, float);
__attribute__((visibility("default"))) double sin(double);
__attribute__((visibility("default"))) float sinf(float);
__attribute__((visibility("default"))) double tan(double);
__attribute__((visibility("default"))) float acosf(float);
__attribute__((visibility("default"))) double atan(double);
__attribute__((visibility("default"))) float atan2f(float, float);
__attribute__((visibility("default"))) float atanf(float);
__attribute__((visibility("default"))) double cbrt(double);
__attribute__((visibility("default"))) float cbrtf(float);
__attribute__((visibility("default"))) double ceil(double);
__attribute__((visibility("default"))) float ceilf(float);
__attribute__((visibility("default"))) double cosh(double);
__attribute__((visibility("default"))) double exp2(double);
__attribute__((visibility("default"))) float exp2f(float);
__attribute__((visibility("default"))) float expf(float);
__attribute__((visibility("default"))) double expm1(double);
__attribute__((visibility("default"))) double fabs(double);
__attribute__((visibility("default"))) float floorf(float);
__attribute__((visibility("default"))) double fmax(double, double);
__attribute__((visibility("default"))) float fmaxf(float, float);
__attribute__((visibility("default"))) float fminf(float, float);
__attribute__((visibility("default"))) double fmod(double, double);
__attribute__((visibility("default"))) float fmodf(float, float);
__attribute__((visibility("default"))) float frexpf(float, int*);
__attribute__((visibility("default"))) double hypot(double, double);
__attribute__((visibility("default"))) float hypotf(float, float);
__attribute__((visibility("default"))) float ldexpf(float, int);
__attribute__((visibility("default"))) long long llrint(double);
__attribute__((visibility("default"))) long long llrintf(float);
__attribute__((visibility("default"))) long long llround(double);
__attribute__((visibility("default"))) long long llroundf(float);
__attribute__((visibility("default"))) double log1p(double);
__attribute__((visibility("default"))) float logf(float);
__attribute__((visibility("default"))) long lrint(double);
__attribute__((visibility("default"))) long lrintf(float);
__attribute__((visibility("default"))) long lround(double);
__attribute__((visibility("default"))) long lroundf(float);
__attribute__((visibility("default"))) double modf(double, double*);
__attribute__((visibility("default"))) float modff(float, float*);
__attribute__((visibility("default"))) float nanf(const char*);
__attribute__((visibility("default"))) double rint(double);
__attribute__((visibility("default"))) float rintf(float);
__attribute__((visibility("default"))) double round(double);
__attribute__((visibility("default"))) float roundf(float);
__attribute__((visibility("default"))) double scalbn(double, int);
__attribute__((visibility("default"))) void sincos(double, double*, double*);
__attribute__((visibility("default"))) void sincosf(float, float*, float*);
__attribute__((visibility("default"))) double sinh(double);
__attribute__((visibility("default"))) float tanf(float);
__attribute__((visibility("default"))) double tanh(double);
__attribute__((visibility("default"))) double trunc(double);
__attribute__((visibility("default"))) float truncf(float);

#define WRAP_UNARY_DOUBLE(name) \
__attribute__((visibility("default"), noinline)) \
double fixture_##name(double value) { return name(value); }

#define WRAP_UNARY_FLOAT(name) \
__attribute__((visibility("default"), noinline)) \
float fixture_##name(float value) { return name(value); }

#define WRAP_BINARY_DOUBLE(name) \
__attribute__((visibility("default"), noinline)) \
double fixture_##name(double first, double second) { return name(first, second); }

#define WRAP_BINARY_FLOAT(name) \
__attribute__((visibility("default"), noinline)) \
float fixture_##name(float first, float second) { return name(first, second); }

WRAP_UNARY_DOUBLE(acos)
WRAP_UNARY_DOUBLE(asin)
WRAP_UNARY_DOUBLE(atan)
WRAP_UNARY_DOUBLE(cbrt)
WRAP_UNARY_DOUBLE(ceil)
WRAP_UNARY_DOUBLE(cos)
WRAP_UNARY_DOUBLE(cosh)
WRAP_UNARY_DOUBLE(exp)
WRAP_UNARY_DOUBLE(exp2)
WRAP_UNARY_DOUBLE(expm1)
WRAP_UNARY_DOUBLE(fabs)
WRAP_UNARY_DOUBLE(floor)
WRAP_UNARY_DOUBLE(log)
WRAP_UNARY_DOUBLE(log10)
WRAP_UNARY_DOUBLE(log1p)
WRAP_UNARY_DOUBLE(rint)
WRAP_UNARY_DOUBLE(round)
WRAP_UNARY_DOUBLE(sin)
WRAP_UNARY_DOUBLE(sinh)
WRAP_UNARY_DOUBLE(tan)
WRAP_UNARY_DOUBLE(tanh)
WRAP_UNARY_DOUBLE(trunc)
WRAP_UNARY_FLOAT(acosf)
WRAP_UNARY_FLOAT(atanf)
WRAP_UNARY_FLOAT(cbrtf)
WRAP_UNARY_FLOAT(ceilf)
WRAP_UNARY_FLOAT(cosf)
WRAP_UNARY_FLOAT(exp2f)
WRAP_UNARY_FLOAT(expf)
WRAP_UNARY_FLOAT(floorf)
WRAP_UNARY_FLOAT(log10f)
WRAP_UNARY_FLOAT(logf)
WRAP_UNARY_FLOAT(rintf)
WRAP_UNARY_FLOAT(roundf)
WRAP_UNARY_FLOAT(sinf)
WRAP_UNARY_FLOAT(tanf)
WRAP_UNARY_FLOAT(truncf)
WRAP_BINARY_DOUBLE(atan2)
WRAP_BINARY_DOUBLE(fmax)
WRAP_BINARY_DOUBLE(fmod)
WRAP_BINARY_DOUBLE(hypot)
WRAP_BINARY_DOUBLE(pow)
WRAP_BINARY_FLOAT(atan2f)
WRAP_BINARY_FLOAT(fmaxf)
WRAP_BINARY_FLOAT(fminf)
WRAP_BINARY_FLOAT(fmodf)
WRAP_BINARY_FLOAT(hypotf)
WRAP_BINARY_FLOAT(powf)

__attribute__((visibility("default"), noinline))
double fixture_frexp(double value, int* exponent) { return frexp(value, exponent); }
__attribute__((visibility("default"), noinline))
float fixture_frexpf(float value, int* exponent) { return frexpf(value, exponent); }
__attribute__((visibility("default"), noinline))
double fixture_ldexp(double value, int exponent) { return ldexp(value, exponent); }
__attribute__((visibility("default"), noinline))
float fixture_ldexpf(float value, int exponent) { return ldexpf(value, exponent); }
__attribute__((visibility("default"), noinline))
long long fixture_llrint(double value) { return llrint(value); }
__attribute__((visibility("default"), noinline))
long long fixture_llrintf(float value) { return llrintf(value); }
__attribute__((visibility("default"), noinline))
long long fixture_llround(double value) { return llround(value); }
__attribute__((visibility("default"), noinline))
long long fixture_llroundf(float value) { return llroundf(value); }
__attribute__((visibility("default"), noinline))
long fixture_lrint(double value) { return lrint(value); }
__attribute__((visibility("default"), noinline))
long fixture_lrintf(float value) { return lrintf(value); }
__attribute__((visibility("default"), noinline))
long fixture_lround(double value) { return lround(value); }
__attribute__((visibility("default"), noinline))
long fixture_lroundf(float value) { return lroundf(value); }
__attribute__((visibility("default"), noinline))
double fixture_modf(double value, double* integral) { return modf(value, integral); }
__attribute__((visibility("default"), noinline))
float fixture_modff(float value, float* integral) { return modff(value, integral); }
__attribute__((visibility("default"), noinline))
float fixture_nanf(const char* tag) { return nanf(tag); }
__attribute__((visibility("default"), noinline))
double fixture_scalbn(double value, int exponent) { return scalbn(value, exponent); }
__attribute__((visibility("default"), noinline))
void fixture_sincos(double value, double* sine, double* cosine) {
    sincos(value, sine, cosine);
}
__attribute__((visibility("default"), noinline))
void fixture_sincosf(float value, float* sine, float* cosine) {
    sincosf(value, sine, cosine);
}
