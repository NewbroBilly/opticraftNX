#pragma once
#include "texture_store.hpp"
#include <cstdint>
#include <vector>

namespace opti::gl {
constexpr std::uint32_t GL_TEXTURE_2D_=0x0DE1;
constexpr std::uint32_t GL_TEXTURE_MIN_FILTER_=0x2801;
constexpr std::uint32_t GL_TEXTURE_MAG_FILTER_=0x2800;
constexpr std::uint32_t GL_TEXTURE_WRAP_S_=0x2802;
constexpr std::uint32_t GL_TEXTURE_WRAP_T_=0x2803;
constexpr std::uint32_t GL_NEAREST_=0x2600;
constexpr std::uint32_t GL_LINEAR_=0x2601;
constexpr std::uint32_t GL_REPEAT_=0x2901;
constexpr std::uint32_t GL_CLAMP_=0x2900;
constexpr std::uint32_t GL_CLAMP_TO_EDGE_=0x812F;
constexpr std::uint32_t GL_UNPACK_ALIGNMENT_=0x0CF5;
constexpr std::uint32_t GL_ALPHA_=0x1906;
constexpr std::uint32_t GL_LUMINANCE_=0x1909;
constexpr std::uint32_t GL_LUMINANCE_ALPHA_=0x190A;
constexpr std::uint32_t GL_RGB_=0x1907;
constexpr std::uint32_t GL_RGBA_=0x1908;
constexpr std::uint32_t GL_UNSIGNED_BYTE_=0x1401;

class TextureApi {
public:
 explicit TextureApi(TextureStore& store):store_(store){}
 std::vector<std::uint32_t> gen(std::size_t count);
 void erase(const std::uint32_t* ids,std::size_t count);
 bool bind(std::uint32_t target,std::uint32_t id);
 bool is_texture(std::uint32_t id) const;
 bool pixel_store(std::uint32_t pname,int value);
 bool parameter_i(std::uint32_t target,std::uint32_t pname,int value);
 bool image_2d(std::uint32_t target,int level,int internal_format,int width,int height,
               int border,std::uint32_t format,std::uint32_t type,
               const std::uint8_t* pixels,std::size_t bytes);
 bool sub_image_2d(std::uint32_t target,int level,int x,int y,int width,int height,
                   std::uint32_t format,std::uint32_t type,
                   const std::uint8_t* pixels,std::size_t bytes);
 std::uint32_t bound_2d() const{return bound_2d_;}
 int unpack_alignment() const{return unpack_alignment_;}
private:
 TextureStore& store_;
 std::uint32_t next_id_=1,bound_2d_=0;
 int unpack_alignment_=4;
 static bool decode_format(std::uint32_t,PixelFormat&);
};
}
