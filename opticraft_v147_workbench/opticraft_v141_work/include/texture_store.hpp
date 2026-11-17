#pragma once
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <vector>
namespace opti::gl {
enum class PixelFormat { Alpha, Luminance, LuminanceAlpha, RGB, RGBA };
enum class TextureFilter { Nearest, Linear };
enum class TextureWrap { Repeat, Clamp };
struct Texture {
 int width=0,height=0;
 TextureFilter min_filter=TextureFilter::Nearest, mag_filter=TextureFilter::Nearest;
 TextureWrap wrap_s=TextureWrap::Repeat, wrap_t=TextureWrap::Repeat;
 std::vector<std::uint8_t> rgba;
 bool complete() const { return width>0 && height>0 && rgba.size()==static_cast<size_t>(width)*height*4; }
};
class TextureStore {
public:
 bool create(std::uint32_t id);
 void erase(std::uint32_t id);
 bool allocate(std::uint32_t id,int width,int height);
 bool upload(std::uint32_t id,int width,int height,PixelFormat format,const std::uint8_t* data,std::size_t bytes,int unpack_alignment=1);
 bool sub_upload(std::uint32_t id,int x,int y,int width,int height,PixelFormat format,const std::uint8_t* data,std::size_t bytes,int unpack_alignment=1);
 bool set_filter(std::uint32_t id, bool magnification, TextureFilter filter);
 bool set_wrap(std::uint32_t id, bool horizontal, TextureWrap wrap);
 const Texture* get(std::uint32_t id) const;
 std::uint32_t sample(std::uint32_t id,float u,float v) const;
 std::size_t size() const { return textures_.size(); }
private:
 std::unordered_map<std::uint32_t,Texture> textures_;
};
}
