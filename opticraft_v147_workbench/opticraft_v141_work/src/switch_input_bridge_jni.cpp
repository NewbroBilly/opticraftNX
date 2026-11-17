#include "switch_input_bridge.hpp"
#include <jni.h>
#include <mutex>
#include <vector>

#if defined(__SWITCH__)
extern "C" void opti_switch_read_input(std::uint64_t*, std::int32_t*, std::int32_t*,
                                        std::int32_t*, std::int32_t*, std::int32_t*,
                                        std::int32_t*, bool*);
#endif

namespace {
opti::SwitchInputBridge bridge;
std::mutex bridge_mutex;

jintArray JNICALL native_update(JNIEnv* env, jclass, jlong buttons,
                                jint lx, jint ly, jint rx, jint ry,
                                jint width, jint height, jint touch_x, jint touch_y, jboolean touch_down) {
    std::lock_guard<std::mutex> lock(bridge_mutex);
    opti::ControllerFrame frame{};
    frame.buttons = static_cast<std::uint64_t>(buttons);
    frame.left_x=lx; frame.left_y=ly; frame.right_x=rx; frame.right_y=ry;
    bridge.update(frame, width, height, touch_x, touch_y, touch_down == JNI_TRUE);
    const auto events = bridge.drain();
    std::vector<jint> packed;
    packed.reserve(events.size()*4);
    for (const auto& e : events) {
        packed.push_back(e.type); packed.push_back(e.code);
        packed.push_back(e.x); packed.push_back(e.y);
    }
    jintArray result=env->NewIntArray(static_cast<jsize>(packed.size()));
    if (result && !packed.empty())
        env->SetIntArrayRegion(result, 0, static_cast<jsize>(packed.size()), packed.data());
    return result;
}
jintArray JNICALL native_poll_from_horizon(JNIEnv* env, jclass, jint width, jint height) {
    std::lock_guard<std::mutex> lock(bridge_mutex);
    opti::ControllerFrame frame{};
    jint touch_x = 0, touch_y = 0;
    bool touch_down = false;
#if defined(__SWITCH__)
    std::uint64_t buttons = 0;
    opti_switch_read_input(&buttons, &frame.left_x, &frame.left_y,
                           &frame.right_x, &frame.right_y,
                           &touch_x, &touch_y, &touch_down);
    frame.buttons = buttons;
#else
    // Host tests deliberately use a neutral snapshot; real polling is libnx-only.
    (void)width; (void)height;
#endif
    bridge.update(frame, width, height, touch_x, touch_y, touch_down);
    const auto events = bridge.drain();
    std::vector<jint> packed;
    packed.reserve(events.size() * 4);
    for (const auto& e : events) {
        packed.push_back(e.type); packed.push_back(e.code);
        packed.push_back(e.x); packed.push_back(e.y);
    }
    jintArray result = env->NewIntArray(static_cast<jsize>(packed.size()));
    if (result && !packed.empty())
        env->SetIntArrayRegion(result, 0, static_cast<jsize>(packed.size()), packed.data());
    return result;
}

jboolean JNICALL native_key_down(JNIEnv*, jclass, jint key) {
    std::lock_guard<std::mutex> lock(bridge_mutex);
    return bridge.key_down(key) ? JNI_TRUE : JNI_FALSE;
}
jboolean JNICALL native_mouse_down(JNIEnv*, jclass, jint button) {
    std::lock_guard<std::mutex> lock(bridge_mutex);
    return bridge.mouse_down(button) ? JNI_TRUE : JNI_FALSE;
}
jint JNICALL native_mouse_x(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(bridge_mutex); return bridge.mouse_x();
}
jint JNICALL native_mouse_y(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(bridge_mutex); return bridge.mouse_y();
}
void JNICALL native_reset(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(bridge_mutex); bridge.reset();
}
const JNINativeMethod methods[] = {
    {const_cast<char*>("update"), const_cast<char*>("(JIIIIIIIIZ)[I"),
     reinterpret_cast<void*>(native_update)},
    {const_cast<char*>("pollFromHorizon"), const_cast<char*>("(II)[I"),
     reinterpret_cast<void*>(native_poll_from_horizon)},
    {const_cast<char*>("isKeyDown"), const_cast<char*>("(I)Z"),
     reinterpret_cast<void*>(native_key_down)},
    {const_cast<char*>("isMouseDown"), const_cast<char*>("(I)Z"),
     reinterpret_cast<void*>(native_mouse_down)},
    {const_cast<char*>("getMouseX"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(native_mouse_x)},
    {const_cast<char*>("getMouseY"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(native_mouse_y)},
    {const_cast<char*>("reset"), const_cast<char*>("()V"),
     reinterpret_cast<void*>(native_reset)}
};
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env=nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6)!=JNI_OK) return JNI_ERR;
    jclass cls=env->FindClass("org/lwjgl/switchport/SwitchInputBridge");
    if (!cls) return JNI_ERR;
    jint rc=env->RegisterNatives(cls, methods, sizeof(methods)/sizeof(methods[0]));
    env->DeleteLocalRef(cls);
    return rc==0 ? JNI_VERSION_1_6 : JNI_ERR;
}
