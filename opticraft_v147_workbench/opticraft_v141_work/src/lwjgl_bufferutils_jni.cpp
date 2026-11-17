#include <jni.h>
#include <cstdint>
#include <cstring>
#include <limits>

#ifndef OPTI_JNI_CLASS
#define OPTI_JNI_CLASS "org/lwjgl/BufferUtils"
#endif

namespace {
jboolean g_lwjgl_debug = JNI_FALSE;

jlong JNICALL native_get_buffer_address(JNIEnv* env, jclass, jobject buffer) {
    if (buffer == nullptr) {
        jclass ex = env->FindClass("java/lang/NullPointerException");
        if (ex) env->ThrowNew(ex, "buffer is null");
        return 0;
    }
    void* address = env->GetDirectBufferAddress(buffer);
    if (address == nullptr) {
        jclass ex = env->FindClass("java/lang/IllegalArgumentException");
        if (ex) env->ThrowNew(ex, "LWJGL requires a direct NIO buffer");
        return 0;
    }
    return static_cast<jlong>(reinterpret_cast<std::intptr_t>(address));
}

jlong element_size(JNIEnv* env, jobject buffer) {
    struct TypeSize { const char* name; jlong size; };
    static const TypeSize types[] = {
        {"java/nio/ByteBuffer", 1},
        {"java/nio/ShortBuffer", 2},
        {"java/nio/CharBuffer", 2},
        {"java/nio/IntBuffer", 4},
        {"java/nio/FloatBuffer", 4},
        {"java/nio/LongBuffer", 8},
        {"java/nio/DoubleBuffer", 8}
    };
    for (const auto& type : types) {
        jclass cls = env->FindClass(type.name);
        if (!cls) {
            env->ExceptionClear();
            continue;
        }
        const bool match = env->IsInstanceOf(buffer, cls);
        env->DeleteLocalRef(cls);
        if (match) return type.size;
    }
    return 0;
}

void JNICALL native_zero_buffer(JNIEnv* env, jclass, jobject buffer,
                                jlong byte_offset, jlong byte_count) {
    if (buffer == nullptr) {
        jclass ex = env->FindClass("java/lang/NullPointerException");
        if (ex) env->ThrowNew(ex, "buffer is null");
        return;
    }
    if (byte_offset < 0 || byte_count < 0) {
        jclass ex = env->FindClass("java/lang/IndexOutOfBoundsException");
        if (ex) env->ThrowNew(ex, "negative offset or length");
        return;
    }
    void* base = env->GetDirectBufferAddress(buffer);
    if (base == nullptr) {
        jclass ex = env->FindClass("java/lang/IllegalArgumentException");
        if (ex) env->ThrowNew(ex, "LWJGL requires a direct NIO buffer");
        return;
    }
    const jlong elem = element_size(env, buffer);
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (elem == 0 || capacity < 0 ||
        capacity > std::numeric_limits<jlong>::max() / elem) {
        jclass ex = env->FindClass("java/lang/IllegalArgumentException");
        if (ex) env->ThrowNew(ex, "unsupported direct buffer type");
        return;
    }
    const jlong byte_capacity = capacity * elem;
    if (byte_offset > byte_capacity ||
        byte_count > byte_capacity - byte_offset) {
        jclass ex = env->FindClass("java/lang/IndexOutOfBoundsException");
        if (ex) env->ThrowNew(ex, "zeroBuffer range exceeds buffer capacity");
        return;
    }
    auto* target = static_cast<unsigned char*>(base) + byte_offset;
    std::memset(target, 0, static_cast<std::size_t>(byte_count));
}


jint JNICALL sys_get_jni_version(JNIEnv*, jobject) { return 19; }
jint JNICALL sys_get_pointer_size(JNIEnv*, jobject) {
    return static_cast<jint>(sizeof(void*));
}
void JNICALL sys_set_debug(JNIEnv*, jobject, jboolean enabled) {
    g_lwjgl_debug = enabled;
}

const JNINativeMethod sys_methods[] = {
    {const_cast<char*>("getJNIVersion"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(sys_get_jni_version)},
    {const_cast<char*>("getPointerSize"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(sys_get_pointer_size)},
    {const_cast<char*>("setDebug"), const_cast<char*>("(Z)V"),
     reinterpret_cast<void*>(sys_set_debug)}
};

const JNINativeMethod methods[] = {
    {const_cast<char*>("zeroBuffer0"),
     const_cast<char*>("(Ljava/nio/Buffer;JJ)V"),
     reinterpret_cast<void*>(native_zero_buffer)},
    {const_cast<char*>("getBufferAddress"),
     const_cast<char*>("(Ljava/nio/Buffer;)J"),
     reinterpret_cast<void*>(native_get_buffer_address)}
};
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;

    jclass buffer_cls = env->FindClass(OPTI_JNI_CLASS);
    if (!buffer_cls) return JNI_ERR;
    const jint buffer_rc = env->RegisterNatives(
        buffer_cls, methods, static_cast<jint>(sizeof(methods) / sizeof(methods[0])));
    env->DeleteLocalRef(buffer_cls);
    if (buffer_rc != 0) return JNI_ERR;
#ifdef OPTI_JNI_TEST
    return JNI_VERSION_1_6;
#endif

    jclass sys_cls = env->FindClass("org/lwjgl/DefaultSysImplementation");
    if (!sys_cls) return JNI_ERR;
    const jint sys_rc = env->RegisterNatives(
        sys_cls, sys_methods,
        static_cast<jint>(sizeof(sys_methods) / sizeof(sys_methods[0])));
    env->DeleteLocalRef(sys_cls);
    return sys_rc == 0 ? JNI_VERSION_1_6 : JNI_ERR;
}
