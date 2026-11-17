#pragma once
#include <cstdint>
#include <cstddef>
using u8=std::uint8_t; using u32=std::uint32_t; using u64=std::uint64_t;
using s32=std::int32_t; using s64=std::int64_t; using Result=int;
#define R_FAILED(x) ((x)!=0)
#define PIXEL_FORMAT_RGBA_8888 1
#define RGBA8(r,g,b,a) ((u32)((r)&255)|((u32)((g)&255)<<8)|((u32)((b)&255)<<16)|((u32)((a)&255)<<24))
struct Framebuffer {};
struct HidTouchState { u64 delta_time; u32 attributes,finger_id,x,y,diameter_x,diameter_y,rotation_angle,reserved; };
struct HidTouchScreenState { u64 sampling_number; s32 count; u32 reserved; HidTouchState touches[16]; };
inline void hidInitializeTouchScreen(){}
inline size_t hidGetTouchScreenStates(HidTouchScreenState*,size_t){return 0;}
enum HidNpadIdType { HidNpadIdType_Handheld, HidNpadIdType_No1 };
enum { HidNpadButton_A=1ull<<0,HidNpadButton_B=1ull<<1,HidNpadButton_X=1ull<<2,HidNpadButton_Y=1ull<<3,HidNpadButton_L=1ull<<6,HidNpadButton_R=1ull<<7,HidNpadButton_Plus=1ull<<10,HidNpadButton_Minus=1ull<<11 };
struct Stick { s32 x,y; };
struct HidNpadFullKeyState { u64 buttons; Stick analog_stick_l,analog_stick_r; };
inline Result hidInitialize(){return 0;} inline void hidInitializeNpad(){} inline void hidExit(){}
inline size_t hidGetNpadStatesHandheld(HidNpadIdType,HidNpadFullKeyState*,size_t){return 0;}
inline size_t hidGetNpadStatesFullKey(HidNpadIdType,HidNpadFullKeyState*,size_t){return 0;}
inline Result framebufferCreate(Framebuffer*,void*,u32,u32,u32,u32){return 0;}
inline Result framebufferMakeLinear(Framebuffer*){return 0;} inline void framebufferClose(Framebuffer*){}
inline void* framebufferBegin(Framebuffer*,u32*){return nullptr;} inline void framebufferEnd(Framebuffer*){}
inline void* nwindowGetDefault(){return nullptr;} inline bool appletMainLoop(){return false;}
