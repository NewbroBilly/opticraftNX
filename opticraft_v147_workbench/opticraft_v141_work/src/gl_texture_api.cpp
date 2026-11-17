#include "gl_texture_api.hpp"
#include <limits>

namespace opti::gl {
bool TextureApi::decode_format(std::uint32_t f,PixelFormat& out){
 switch(f){case GL_ALPHA_:out=PixelFormat::Alpha;return true;
 case GL_LUMINANCE_:out=PixelFormat::Luminance;return true;
 case GL_LUMINANCE_ALPHA_:out=PixelFormat::LuminanceAlpha;return true;
 case GL_RGB_:out=PixelFormat::RGB;return true;
 case GL_RGBA_:out=PixelFormat::RGBA;return true;default:return false;}
}
std::vector<std::uint32_t> TextureApi::gen(std::size_t n){
 std::vector<std::uint32_t> ids; if(n>65536)return ids; ids.reserve(n);
 for(std::size_t i=0;i<n;i++){
  while(next_id_==0||store_.get(next_id_))++next_id_;
  const auto id=next_id_++; if(store_.create(id))ids.push_back(id);
 }
 return ids;
}
void TextureApi::erase(const std::uint32_t* ids,std::size_t n){
 if(!ids) return;
 for(std::size_t i=0;i<n;i++) {
  store_.erase(ids[i]);
  if(bound_2d_==ids[i]) bound_2d_=0;
 }
}
bool TextureApi::bind(std::uint32_t target,std::uint32_t id){
 if(target!=GL_TEXTURE_2D_)return false;
 if(id&&!store_.get(id)&&!store_.create(id))return false;
 bound_2d_=id;return true;
}
bool TextureApi::is_texture(std::uint32_t id)const{return id!=0&&store_.get(id)!=nullptr;}
bool TextureApi::pixel_store(std::uint32_t pname,int value){
 if(pname!=GL_UNPACK_ALIGNMENT_||(value!=1&&value!=2&&value!=4&&value!=8))return false;
 unpack_alignment_=value;return true;
}
bool TextureApi::parameter_i(std::uint32_t target,std::uint32_t pname,int value){
 if(target!=GL_TEXTURE_2D_||!bound_2d_)return false;
 if(pname==GL_TEXTURE_MIN_FILTER_||pname==GL_TEXTURE_MAG_FILTER_){
  if(value!=int(GL_NEAREST_)&&value!=int(GL_LINEAR_))return false;
  return store_.set_filter(bound_2d_,pname==GL_TEXTURE_MAG_FILTER_,
                           value==int(GL_NEAREST_)?TextureFilter::Nearest:TextureFilter::Linear);
 }
 if(pname==GL_TEXTURE_WRAP_S_||pname==GL_TEXTURE_WRAP_T_){
  if(value!=int(GL_REPEAT_)&&value!=int(GL_CLAMP_)&&value!=int(GL_CLAMP_TO_EDGE_))return false;
  return store_.set_wrap(bound_2d_,pname==GL_TEXTURE_WRAP_S_,
                         value==int(GL_REPEAT_)?TextureWrap::Repeat:TextureWrap::Clamp);
 }
 return false;
}
bool TextureApi::image_2d(std::uint32_t target,int level,int internal,int w,int h,int border,
                          std::uint32_t format,std::uint32_t type,const std::uint8_t* pixels,
                          std::size_t bytes){
 if(target!=GL_TEXTURE_2D_||level!=0||border!=0||!bound_2d_||type!=GL_UNSIGNED_BYTE_)return false;
 PixelFormat f;if(!decode_format(format,f))return false;
 (void)internal;
 if(!pixels)return store_.allocate(bound_2d_,w,h);
 return store_.upload(bound_2d_,w,h,f,pixels,bytes,unpack_alignment_);
}
bool TextureApi::sub_image_2d(std::uint32_t target,int level,int x,int y,int w,int h,
                              std::uint32_t format,std::uint32_t type,const std::uint8_t* pixels,
                              std::size_t bytes){
 if(target!=GL_TEXTURE_2D_||level!=0||!bound_2d_||type!=GL_UNSIGNED_BYTE_)return false;
 PixelFormat f;if(!decode_format(format,f))return false;
 return store_.sub_upload(bound_2d_,x,y,w,h,f,pixels,bytes,unpack_alignment_);
}
}
