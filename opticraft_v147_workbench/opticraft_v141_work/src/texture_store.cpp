#include "texture_store.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace opti::gl {
bool TextureStore::create(std::uint32_t id) { if(!id) return false; return textures_.emplace(id,Texture{}).second; }
void TextureStore::erase(std::uint32_t id) { textures_.erase(id); }
namespace {
std::size_t channel_count(PixelFormat f) {
 switch(f) {
  case PixelFormat::Alpha: case PixelFormat::Luminance:return 1;
  case PixelFormat::LuminanceAlpha:return 2;
  case PixelFormat::RGB:return 3;
  case PixelFormat::RGBA:return 4;
 }
 return 0;
}
bool convert_pixels(std::vector<std::uint8_t>& out,int w,int h,PixelFormat f,
                    const std::uint8_t* src,std::size_t bytes,int alignment) {
 if(!src||w<=0||h<=0||w>4096||h>4096||
    (alignment!=1&&alignment!=2&&alignment!=4&&alignment!=8)) return false;
 const std::size_t channels=channel_count(f);
 if(!channels) return false;
 const std::size_t row_bytes=static_cast<std::size_t>(w)*channels;
 const std::size_t stride=(row_bytes+static_cast<std::size_t>(alignment-1))&
                          ~static_cast<std::size_t>(alignment-1);
 if(static_cast<std::size_t>(h-1)>0 &&
    stride>(std::numeric_limits<std::size_t>::max()-row_bytes)/static_cast<std::size_t>(h-1)) return false;
 const std::size_t required=static_cast<std::size_t>(h-1)*stride+row_bytes;
 const std::size_t pixels=static_cast<std::size_t>(w)*static_cast<std::size_t>(h);
 if(bytes<required||pixels>std::numeric_limits<std::size_t>::max()/4) return false;
 out.resize(pixels*4);
 for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
  const auto* p=src+static_cast<std::size_t>(y)*stride+static_cast<std::size_t>(x)*channels;
  auto* q=out.data()+(static_cast<std::size_t>(y)*w+x)*4;
  switch(f) {
   case PixelFormat::Alpha:q[0]=q[1]=q[2]=255;q[3]=p[0];break;
   case PixelFormat::Luminance:q[0]=q[1]=q[2]=p[0];q[3]=255;break;
   case PixelFormat::LuminanceAlpha:q[0]=q[1]=q[2]=p[0];q[3]=p[1];break;
   case PixelFormat::RGB:q[0]=p[0];q[1]=p[1];q[2]=p[2];q[3]=255;break;
   case PixelFormat::RGBA:q[0]=p[0];q[1]=p[1];q[2]=p[2];q[3]=p[3];break;
  }
 }
 return true;
}
}
bool TextureStore::allocate(std::uint32_t id,int w,int h) {
 auto it=textures_.find(id);
 if(it==textures_.end()||w<=0||h<=0||w>4096||h>4096) return false;
 const std::size_t pixels=static_cast<std::size_t>(w)*static_cast<std::size_t>(h);
 if(pixels>std::numeric_limits<std::size_t>::max()/4) return false;
 it->second.width=w;it->second.height=h;it->second.rgba.assign(pixels*4,0);
 return true;
}
bool TextureStore::upload(std::uint32_t id,int w,int h,PixelFormat f,const std::uint8_t* src,
                          std::size_t bytes,int alignment) {
 auto it=textures_.find(id); if(it==textures_.end()) return false;
 std::vector<std::uint8_t> rgba;
 if(!convert_pixels(rgba,w,h,f,src,bytes,alignment)) return false;
 it->second.width=w;it->second.height=h;it->second.rgba=std::move(rgba);return true;
}
bool TextureStore::sub_upload(std::uint32_t id,int x,int y,int w,int h,PixelFormat f,
                              const std::uint8_t* src,std::size_t bytes,int alignment) {
 auto it=textures_.find(id);
 if(it==textures_.end()||!it->second.complete()||x<0||y<0||
    w<=0||h<=0||x>it->second.width-w||y>it->second.height-h) return false;
 std::vector<std::uint8_t> patch;
 if(!convert_pixels(patch,w,h,f,src,bytes,alignment)) return false;
 for(int row=0;row<h;row++) {
  const auto dst=(static_cast<std::size_t>(y+row)*it->second.width+x)*4;
  const auto from=static_cast<std::size_t>(row)*w*4;
  std::copy_n(patch.data()+from,static_cast<std::size_t>(w)*4,it->second.rgba.data()+dst);
 }
 return true;
}
bool TextureStore::set_filter(std::uint32_t id,bool mag,TextureFilter f) {
 auto it=textures_.find(id);if(it==textures_.end())return false;
 if(mag) it->second.mag_filter=f; else it->second.min_filter=f; return true;
}
bool TextureStore::set_wrap(std::uint32_t id,bool horizontal,TextureWrap w) {
 auto it=textures_.find(id);if(it==textures_.end())return false;
 if(horizontal)it->second.wrap_s=w;else it->second.wrap_t=w;return true;
}
const Texture* TextureStore::get(std::uint32_t id) const {
 auto it=textures_.find(id);return it==textures_.end()?nullptr:&it->second;
}
std::uint32_t TextureStore::sample(std::uint32_t id,float u,float v) const {
 const Texture* t=get(id);if(!t||!t->complete())return 0xFFFFFFFFu;
 auto coord=[](float x,TextureWrap w,int n) {
  if(w==TextureWrap::Repeat)x=x-std::floor(x);
  else x=std::clamp(x,0.0f,1.0f);
  // Normalized GL texture coordinates address texel centers at (i + 0.5) / n.
  return x*n-0.5f;
 };
 float x=coord(u,t->wrap_s,t->width),y=coord(v,t->wrap_t,t->height);
 auto pixel=[&](int px,int py)->std::uint32_t {
  if(t->wrap_s==TextureWrap::Repeat){px%=t->width;if(px<0)px+=t->width;}else px=std::clamp(px,0,t->width-1);
  if(t->wrap_t==TextureWrap::Repeat){py%=t->height;if(py<0)py+=t->height;}else py=std::clamp(py,0,t->height-1);
  const auto* p=&t->rgba[(static_cast<std::size_t>(py)*t->width+px)*4];
  return p[0]|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
 };
 if(t->mag_filter==TextureFilter::Nearest && t->min_filter==TextureFilter::Nearest)
  return pixel(static_cast<int>(std::floor(x+0.5f)),static_cast<int>(std::floor(y+0.5f)));
 int x0=static_cast<int>(std::floor(x)),y0=static_cast<int>(std::floor(y));
 float fx=x-x0,fy=y-y0; auto a=pixel(x0,y0),b=pixel(x0+1,y0),c=pixel(x0,y0+1),d=pixel(x0+1,y0+1);
 std::uint32_t out=0;
 for(int k=0;k<4;k++){float av=float((a>>(8*k))&255),bv=float((b>>(8*k))&255),cv=float((c>>(8*k))&255),dv=float((d>>(8*k))&255);
  auto q=static_cast<std::uint32_t>(std::clamp(std::lround((av*(1-fx)+bv*fx)*(1-fy)+(cv*(1-fx)+dv*fx)*fy),0l,255l));out|=q<<(8*k);}
 return out;
}
}
