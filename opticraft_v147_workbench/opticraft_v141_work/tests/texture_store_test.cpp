#include "texture_store.hpp"
#include <cassert>
#include <cstdint>
using namespace opti::gl;
int main(){
 TextureStore s; assert(s.create(1)); assert(!s.create(1));
 const std::uint8_t rgb[]={255,0,0, 0,255,0, 0,0,255, 255,255,255};
 assert(s.upload(1,2,2,PixelFormat::RGB,rgb,sizeof(rgb)));
 auto t=s.get(1);assert(t&&t->complete()&&t->width==2);
 auto p=s.sample(1,0.0f,0.0f);assert((p&255)==255&&((p>>8)&255)==0);
 assert(s.set_filter(1,true,TextureFilter::Linear));
 assert(s.set_wrap(1,true,TextureWrap::Clamp));
 const std::uint8_t la[]={64,128};
 assert(s.create(2));assert(s.upload(2,1,1,PixelFormat::LuminanceAlpha,la,sizeof(la)));
 auto q=s.sample(2,0.5f,0.5f);assert((q&255)==64&&((q>>8)&255)==64&&((q>>24)&255)==128);
 assert(!s.upload(2,2,2,PixelFormat::RGBA,la,sizeof(la)));
 assert(s.set_filter(1,true,TextureFilter::Nearest));assert(s.set_filter(1,false,TextureFilter::Nearest));
 // GL-style sub-image update: replace the top-right texel without reallocating.
 const std::uint8_t cyan[]={0,255,255};
 assert(s.sub_upload(1,1,0,1,1,PixelFormat::RGB,cyan,sizeof(cyan)));
 auto updated=s.sample(1,0.75f,0.25f);
 assert((updated&255)==0&&((updated>>8)&255)==255&&((updated>>16)&255)==255);
 // RGB rows obey GL_UNPACK_ALIGNMENT: 1x2 RGB with alignment 4 has 1 padding byte.
 const std::uint8_t padded[]={10,20,30,99,40,50,60};
 assert(s.create(3));assert(s.upload(3,1,2,PixelFormat::RGB,padded,sizeof(padded),4));
 auto lower=s.sample(3,0.5f,0.75f);
 assert((lower&255)==40&&((lower>>8)&255)==50&&((lower>>16)&255)==60);
 assert(!s.sub_upload(1,1,1,2,1,PixelFormat::RGB,cyan,sizeof(cyan)));
 s.erase(1);assert(!s.get(1));return 0;
}
