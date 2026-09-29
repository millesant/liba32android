typedef int jint;
typedef void* jobject;
typedef jobject jclass;
typedef jobject jarray;
typedef jobject jobjectArray;
typedef jobject jfieldID;
typedef jobject jstring;
typedef jobject jlongArray;
typedef long long jlong;
typedef unsigned char jboolean;

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
    void* reserved151_to_166[16];
    jstring (*NewStringUTF)(JNIEnv*, const char*);
    void* reserved168;
    const char* (*GetStringUTFChars)(JNIEnv*, jstring, jboolean*);
    void (*ReleaseStringUTFChars)(JNIEnv*, jstring, const char*);
    jint (*GetArrayLength)(JNIEnv*, jarray);
    jobjectArray (*NewObjectArray)(
        JNIEnv*, jint, jclass, jobject);
    jobject (*GetObjectArrayElement)(
        JNIEnv*, jobjectArray, jint);
    void (*SetObjectArrayElement)(
        JNIEnv*, jobjectArray, jint, jobject);
    void* reserved175_to_179[5];
    jlongArray (*NewLongArray)(JNIEnv*, jint);
    void* reserved181_to_187[7];
    jlong* (*GetLongArrayElements)(JNIEnv*, jlongArray, jboolean*);
    void* reserved189_to_195[7];
    void (*ReleaseLongArrayElements)(
        JNIEnv*, jlongArray, jlong*, jint);
    void* reserved197_to_211[15];
    void (*SetLongArrayRegion)(
        JNIEnv*, jlongArray, jint, jint, const jlong*);
    void* reserved213_to_214[2];
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
    jstring message;
    const char* message_chars;
    jlongArray long_array;
    jobjectArray object_array;
    jobject object_element;
    jlong long_values[3];
    jlong* long_elements;
    jboolean long_is_copy;
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

    message = env->functions->NewStringUTF(env, "hello");
    if (message == (jstring)0) {
        return JNI_ERR;
    }
    message_chars = env->functions->GetStringUTFChars(
        env,
        message,
        (jboolean*)0);
    if (message_chars == (const char*)0 ||
        message_chars[0] != 'h' ||
        message_chars[1] != 'e' ||
        message_chars[2] != 'l' ||
        message_chars[3] != 'l' ||
        message_chars[4] != 'o' ||
        message_chars[5] != '\0') {
        return JNI_ERR;
    }
    env->functions->ReleaseStringUTFChars(
        env,
        message,
        message_chars);
    env->functions->DeleteLocalRef(env, message);

    long_array = env->functions->NewLongArray(env, 3);
    if (long_array == (jlongArray)0) {
        return JNI_ERR;
    }
    long_values[0] = 1;
    long_values[1] = -2;
    long_values[2] = 42;
    env->functions->SetLongArrayRegion(
        env,
        long_array,
        0,
        3,
        long_values);
    long_is_copy = 0;
    long_elements = env->functions->GetLongArrayElements(
        env,
        long_array,
        &long_is_copy);
    if (long_elements == (jlong*)0 ||
        long_is_copy == 0 ||
        long_elements[0] != 1 ||
        long_elements[1] != -2 ||
        long_elements[2] != 42) {
        return JNI_ERR;
    }
    long_elements[1] = 7;
    env->functions->ReleaseLongArrayElements(
        env,
        long_array,
        long_elements,
        0);
    long_elements = env->functions->GetLongArrayElements(
        env,
        long_array,
        (jboolean*)0);
    if (long_elements == (jlong*)0 ||
        long_elements[0] != 1 ||
        long_elements[1] != 7 ||
        long_elements[2] != 42) {
        return JNI_ERR;
    }
    env->functions->ReleaseLongArrayElements(
        env,
        long_array,
        long_elements,
        2);
    env->functions->DeleteLocalRef(env, long_array);

    object_array = env->functions->NewObjectArray(
        env,
        2,
        fixture_class,
        (jobject)0);
    if (object_array == (jobjectArray)0) {
        return JNI_ERR;
    }
    env->functions->SetObjectArrayElement(
        env,
        object_array,
        0,
        fixture_class);
    object_element = env->functions->GetObjectArrayElement(
        env,
        object_array,
        0);
    if (object_element != (jobject)fixture_class) {
        return JNI_ERR;
    }
    env->functions->DeleteLocalRef(env, object_element);
    env->functions->DeleteLocalRef(env, object_array);

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
