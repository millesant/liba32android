// Freestanding ARMv7/Android consumer for feature-047 partial libm.so.

__attribute__((visibility("default"))) double acos(double value);
__attribute__((visibility("default"))) double asin(double value);
__attribute__((visibility("default"))) double atan2(double y, double x);
__attribute__((visibility("default"))) double cos(double value);
__attribute__((visibility("default"))) float cosf(float value);
__attribute__((visibility("default"))) double exp(double value);
__attribute__((visibility("default"))) double floor(double value);
__attribute__((visibility("default"))) double frexp(double value, int* exponent);
__attribute__((visibility("default"))) double ldexp(double value, int exponent);
__attribute__((visibility("default"))) double log(double value);
__attribute__((visibility("default"))) double log10(double value);
__attribute__((visibility("default"))) float log10f(float value);
__attribute__((visibility("default"))) double pow(double base, double exponent);
__attribute__((visibility("default"))) float powf(float base, float exponent);
__attribute__((visibility("default"))) double sin(double value);
__attribute__((visibility("default"))) float sinf(float value);
__attribute__((visibility("default"))) double tan(double value);

#define WRAP_UNARY_DOUBLE(name) \
__attribute__((visibility("default"), noinline)) \
double fixture_##name(double value) { return name(value); }

WRAP_UNARY_DOUBLE(acos)
WRAP_UNARY_DOUBLE(asin)
WRAP_UNARY_DOUBLE(cos)
WRAP_UNARY_DOUBLE(exp)
WRAP_UNARY_DOUBLE(floor)
WRAP_UNARY_DOUBLE(log)
WRAP_UNARY_DOUBLE(log10)
WRAP_UNARY_DOUBLE(sin)
WRAP_UNARY_DOUBLE(tan)

__attribute__((visibility("default"), noinline))
double fixture_atan2(double y, double x) {
    return atan2(y, x);
}

__attribute__((visibility("default"), noinline))
float fixture_cosf(float value) {
    return cosf(value);
}

__attribute__((visibility("default"), noinline))
double fixture_frexp(double value, int* exponent) {
    return frexp(value, exponent);
}

__attribute__((visibility("default"), noinline))
double fixture_ldexp(double value, int exponent) {
    return ldexp(value, exponent);
}

__attribute__((visibility("default"), noinline))
float fixture_log10f(float value) {
    return log10f(value);
}

__attribute__((visibility("default"), noinline))
double fixture_pow(double base, double exponent) {
    return pow(base, exponent);
}

__attribute__((visibility("default"), noinline))
float fixture_powf(float base, float exponent) {
    return powf(base, exponent);
}

__attribute__((visibility("default"), noinline))
float fixture_sinf(float value) {
    return sinf(value);
}
