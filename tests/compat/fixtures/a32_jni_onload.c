typedef int jint;

typedef struct JNIEnv_ JNIEnv;
typedef struct JavaVM_ JavaVM;

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

#define JNI_OK 0
#define JNI_ERR (-1)
#define JNI_VERSION_1_6 0x00010006

__attribute__((visibility("default"), noinline))
jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    void* env = (void*)0;
    if (vm == (JavaVM*)0 || reserved != (void*)0) {
        return JNI_ERR;
    }
    if (vm->functions->GetEnv(
            vm,
            &env,
            JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    if (env == (void*)0) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}
