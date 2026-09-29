typedef int jint;
typedef void* jobject;
typedef jobject jclass;

typedef struct JNIEnv_ JNIEnv;
typedef struct JavaVM_ JavaVM;

typedef struct JNINativeMethod {
    const char* name;
    const char* signature;
    void* fnPtr;
} JNINativeMethod;

struct JNINativeInterface {
    void* reserved0;
    void* reserved1;
    void* reserved2;
    void* reserved3;
    void* get_version;
    void* define_class;
    jclass (*FindClass)(JNIEnv*, const char*);
    void* reserved7_to_214[208];
    jint (*RegisterNatives)(
        JNIEnv*,
        jclass,
        const JNINativeMethod*,
        jint);
};

struct JNIEnv_ {
    const struct JNINativeInterface* functions;
};

struct JNIInvokeInterface {
    void* reserved0;
    void* reserved1;
    void* reserved2;
    jint (*DestroyJavaVM)(JavaVM*);
    jint (*AttachCurrentThread)(JavaVM*, JNIEnv**, void*);
    jint (*DetachCurrentThread)(JavaVM*);
    jint (*GetEnv)(JavaVM*, void**, jint);
    jint (*AttachCurrentThreadAsDaemon)(JavaVM*, JNIEnv**, void*);
};

struct JavaVM_ {
    const struct JNIInvokeInterface* functions;
};

typedef char JNINativeMethod_must_be_12_bytes[
    sizeof(JNINativeMethod) == 12 ? 1 : -1];
typedef char JNINativeInterface_must_be_216_words[
    sizeof(struct JNINativeInterface) == (216 * 4) ? 1 : -1];

#define JNI_OK 0
#define JNI_ERR (-1)
#define JNI_VERSION_1_6 0x00010006

__attribute__((noinline))
static jint native_ping(JNIEnv* env, jobject receiver) {
    if (env == (JNIEnv*)0 || receiver == (jobject)0) {
        return JNI_ERR;
    }
    return 42;
}

__attribute__((visibility("default"), noinline))
jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = (JNIEnv*)0;
    jclass fixture_class;
    const JNINativeMethod methods[1] = {
        {
            "nativePing",
            "()I",
            (void*)native_ping,
        },
    };

    if (vm == (JavaVM*)0 || reserved != (void*)0) {
        return JNI_ERR;
    }
    if (vm->functions->GetEnv(
            vm,
            (void**)&env,
            JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    if (env == (JNIEnv*)0 || env->functions == (void*)0) {
        return JNI_ERR;
    }

    fixture_class = env->functions->FindClass(
        env,
        "org/videolan/Fixture");
    if (fixture_class == (jclass)0) {
        return JNI_ERR;
    }
    if (env->functions->RegisterNatives(
            env,
            fixture_class,
            methods,
            1) != JNI_OK) {
        return JNI_ERR;
    }

    return JNI_VERSION_1_6;
}
