typedef int jint;
typedef void* jobject;
typedef jobject jclass;
typedef jobject jarray;
typedef jobject jfieldID;

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
    void* reserved7_to_20[14];
    jobject (*NewGlobalRef)(JNIEnv*, jobject);
    void (*DeleteGlobalRef)(JNIEnv*, jobject);
    void (*DeleteLocalRef)(JNIEnv*, jobject);
    void* reserved24_to_143[120];
    jfieldID (*GetStaticFieldID)(
        JNIEnv*, jclass, const char*, const char*);
    void* reserved145_to_149[5];
    jint (*GetStaticIntField)(JNIEnv*, jclass, jfieldID);
    void* reserved151_to_170[20];
    jint (*GetArrayLength)(JNIEnv*, jarray);
    void* reserved172_to_214[43];
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
    jobject global_class;
    jfieldID answer_field;
    JNINativeMethod method;

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
    if (env->functions->GetArrayLength(
            env,
            (jarray)0x44560000) != 7) {
        return JNI_ERR;
    }

    fixture_class = env->functions->FindClass(
        env,
        "org/videolan/Fixture");
    if (fixture_class == (jclass)0) {
        return JNI_ERR;
    }

    answer_field = env->functions->GetStaticFieldID(
        env,
        fixture_class,
        "answer",
        "I");
    if (answer_field == (jfieldID)0 ||
        env->functions->GetStaticIntField(
            env,
            fixture_class,
            answer_field) != 42) {
        return JNI_ERR;
    }

    global_class = env->functions->NewGlobalRef(
        env,
        fixture_class);
    if (global_class == (jobject)0) {
        return JNI_ERR;
    }
    env->functions->DeleteLocalRef(env, fixture_class);

    method.name = "nativePing";
    method.signature = "()I";
    method.fnPtr = (void*)native_ping;
    if (env->functions->RegisterNatives(
            env,
            (jclass)global_class,
            &method,
            1) != JNI_OK) {
        return JNI_ERR;
    }
    env->functions->DeleteGlobalRef(env, global_class);

    return JNI_VERSION_1_6;
}
