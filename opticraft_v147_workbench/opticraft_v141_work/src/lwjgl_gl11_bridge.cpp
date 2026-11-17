#include "lwjgl_gl11_bridge.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace opti::gl {
namespace {
constexpr std::uint32_t GL_NO_ERROR=0,GL_INVALID_ENUM=0x0500,GL_INVALID_VALUE=0x0501,
 GL_INVALID_OPERATION=0x0502,GL_OUT_OF_MEMORY=0x0505;
constexpr std::uint32_t GL_COLOR_BUFFER_BIT=0x00004000,GL_DEPTH_BUFFER_BIT=0x00000100;
constexpr std::uint32_t GL_VENDOR=0x1F00,GL_RENDERER=0x1F01,GL_VERSION=0x1F02,GL_EXTENSIONS=0x1F03;
constexpr std::uint32_t GL_DEPTH_TEST=0x0B71,GL_BLEND=0x0BE2,GL_CULL_FACE=0x0B44,
 GL_TEXTURE_2D=0x0DE1,GL_FOG=0x0B60,GL_LIGHTING=0x0B50,GL_ALPHA_TEST=0x0BC0;
}
LwjglGL11Bridge::LwjglGL11Bridge(int w,int h):context_(w,h){}
bool LwjglGL11Bridge::valid_dimensions(int w,int h)const{return w>0&&h>0&&w<=4096&&h<=4096;}
void LwjglGL11Bridge::resize(int w,int h){if(!valid_dimensions(w,h)){set_error(GL_INVALID_VALUE);return;}context_.resize(w,h);}
void LwjglGL11Bridge::display_swap_buffers(){if(present_)present_(context_.renderer().color_buffer().data(),context_.renderer().width(),context_.renderer().height());}
bool LwjglGL11Bridge::mode(std::uint32_t m,Primitive& p){switch(m){case 0:p=POINTS;return true;case 1:p=LINES;return true;case 2:p=LINE_LOOP;return true;case 3:p=LINE_STRIP;return true;case 4:p=TRIANGLES;return true;case 5:p=TRIANGLE_STRIP;return true;case 6:p=TRIANGLE_FAN;return true;case 7:p=QUADS;return true;default:return false;}}
void LwjglGL11Bridge::begin(std::uint32_t m){Primitive p;if(!mode(m,p)){set_error(GL_INVALID_ENUM);return;}try{context_.begin(p);}catch(...){set_error(GL_INVALID_OPERATION);}}
void LwjglGL11Bridge::end(){try{if(!context_.end())set_error(GL_INVALID_OPERATION);}catch(...){set_error(GL_INVALID_OPERATION);}}
void LwjglGL11Bridge::color3f(float r,float g,float b){context_.color(r,g,b,1);}
void LwjglGL11Bridge::color4f(float r,float g,float b,float a){context_.color(r,g,b,a);}
void LwjglGL11Bridge::tex_coord2f(float u,float v){context_.texcoord(u,v);} void LwjglGL11Bridge::normal3f(float x,float y,float z){context_.normal(x,y,z);} void LwjglGL11Bridge::vertex3f(float x,float y,float z){try{context_.vertex(x,y,z);}catch(...){set_error(GL_INVALID_OPERATION);}}
void LwjglGL11Bridge::matrix_mode(std::uint32_t m){try{context_.core().matrix_mode(m);}catch(...){set_error(GL_INVALID_ENUM);}}
void LwjglGL11Bridge::load_identity(){try{context_.core().load_identity();}catch(...){set_error(GL_INVALID_OPERATION);}}
void LwjglGL11Bridge::load_matrix(const std::array<float,16>& values){try{context_.core().load_matrix(values);}catch(...){set_error(GL_INVALID_OPERATION);}}
void LwjglGL11Bridge::mult_matrix(const std::array<float,16>& values){try{context_.core().mult_matrix(values);}catch(...){set_error(GL_INVALID_OPERATION);}}
void LwjglGL11Bridge::push_matrix(){try{context_.core().push_matrix();}catch(...){set_error(GL_INVALID_OPERATION);}}
void LwjglGL11Bridge::pop_matrix(){try{context_.core().pop_matrix();}catch(...){set_error(GL_INVALID_OPERATION);}}
void LwjglGL11Bridge::translatef(float x,float y,float z){context_.core().translate(x,y,z);} void LwjglGL11Bridge::scalef(float x,float y,float z){context_.core().scale(x,y,z);} void LwjglGL11Bridge::rotatef(float a,float x,float y,float z){context_.core().rotate(a,x,y,z);}
void LwjglGL11Bridge::ortho(double l,double r,double b,double t,double n,double f){try{context_.core().ortho(l,r,b,t,n,f);}catch(...){set_error(GL_INVALID_VALUE);}}
void LwjglGL11Bridge::viewport(int x,int y,int w,int h){if(w<0||h<0){set_error(GL_INVALID_VALUE);return;}context_.core().viewport(x,y,w,h);}
void LwjglGL11Bridge::clear_color(float r,float g,float b,float a){context_.core().clear_color(r,g,b,a);} void LwjglGL11Bridge::clear_depth(double d){context_.core().clear_depth(static_cast<float>(d));}
void LwjglGL11Bridge::clear(std::uint32_t mask){const auto known=GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT;if(mask&~known){set_error(GL_INVALID_VALUE);return;}context_.clear((mask&GL_COLOR_BUFFER_BIT)!=0,(mask&GL_DEPTH_BUFFER_BIT)!=0);}
void LwjglGL11Bridge::enable(std::uint32_t c){switch(c){case GL_DEPTH_TEST:case GL_BLEND:case GL_CULL_FACE:case GL_TEXTURE_2D:case GL_FOG:case GL_LIGHTING:case GL_ALPHA_TEST:context_.core().enable(c,true);break;default:set_error(GL_INVALID_ENUM);}}
void LwjglGL11Bridge::disable(std::uint32_t c){switch(c){case GL_DEPTH_TEST:case GL_BLEND:case GL_CULL_FACE:case GL_TEXTURE_2D:case GL_FOG:case GL_LIGHTING:case GL_ALPHA_TEST:context_.core().enable(c,false);break;default:set_error(GL_INVALID_ENUM);}}
void LwjglGL11Bridge::depth_func(std::uint32_t f){try{context_.core().depth_func(f);}catch(...){set_error(GL_INVALID_ENUM);}}
void LwjglGL11Bridge::blend_func(std::uint32_t s,std::uint32_t d){try{context_.core().blend_func(s,d);}catch(...){set_error(GL_INVALID_ENUM);}}
void LwjglGL11Bridge::alpha_func(std::uint32_t f,float r){try{context_.core().alpha_func(f,r);}catch(...){set_error(GL_INVALID_ENUM);}}
void LwjglGL11Bridge::color_mask(bool r,bool g,bool b,bool a){context_.core().color_mask(r,g,b,a);}
void LwjglGL11Bridge::pixel_store_i(std::uint32_t p,int v){if(!context_.texture_api().pixel_store(p,v))set_error(GL_INVALID_ENUM);}
std::vector<std::uint32_t> LwjglGL11Bridge::gen_textures(std::size_t n){auto ids=context_.texture_api().gen(n);if(ids.size()!=n)set_error(GL_OUT_OF_MEMORY);return ids;}
void LwjglGL11Bridge::delete_textures(const std::uint32_t* p,std::size_t n){if(!p&&n){set_error(GL_INVALID_VALUE);return;}context_.texture_api().erase(p,n);}
void LwjglGL11Bridge::bind_texture(std::uint32_t t,std::uint32_t id){if(!context_.texture_api().bind(t,id))set_error(GL_INVALID_ENUM);}
void LwjglGL11Bridge::tex_parameter_i(std::uint32_t t,std::uint32_t p,int v){if(!context_.texture_api().parameter_i(t,p,v))set_error(GL_INVALID_ENUM);}
void LwjglGL11Bridge::tex_image_2d(std::uint32_t t,int l,int i,int w,int h,int b,std::uint32_t f,std::uint32_t ty,const std::uint8_t* p,std::size_t n){if(w<0||h<0){set_error(GL_INVALID_VALUE);return;}if(!context_.texture_api().image_2d(t,l,i,w,h,b,f,ty,p,n))set_error(GL_INVALID_OPERATION);}
void LwjglGL11Bridge::tex_sub_image_2d(std::uint32_t t,int l,int x,int y,int w,int h,std::uint32_t f,std::uint32_t ty,const std::uint8_t* p,std::size_t n){if(!context_.texture_api().sub_image_2d(t,l,x,y,w,h,f,ty,p,n))set_error(GL_INVALID_OPERATION);}
void LwjglGL11Bridge::enable_client_state(std::uint32_t a){if(!context_.arrays().enable(a,true))set_error(GL_INVALID_ENUM);} void LwjglGL11Bridge::disable_client_state(std::uint32_t a){if(!context_.arrays().enable(a,false))set_error(GL_INVALID_ENUM);}
bool LwjglGL11Bridge::set_client_array_bytes(std::uint32_t array,int components,ArrayScalar scalar,int stride,const std::uint8_t* bytes,std::size_t size){
    std::size_t slot=0;switch(array){case 0x8074:slot=0;break;case 0x8076:slot=1;break;case 0x8078:slot=2;break;case 0x8075:slot=3;break;default:return false;}
    if(!bytes&&size)return false;
    if(size)client_array_storage_[slot].assign(bytes,bytes+size); else client_array_storage_[slot].clear();
    const void* data=client_array_storage_[slot].empty()?nullptr:client_array_storage_[slot].data();
    switch(array){case 0x8074:return context_.arrays().vertex_pointer(components,scalar,stride,data);case 0x8076:return context_.arrays().color_pointer(components,scalar,stride,data);case 0x8078:return context_.arrays().texcoord_pointer(components,scalar,stride,data);case 0x8075:return context_.arrays().normal_pointer(scalar,stride,data);default:return false;}
}
void LwjglGL11Bridge::vertex_pointer(int n,ArrayScalar t,int s,const void* p){if(!context_.arrays().vertex_pointer(n,t,s,p))set_error(GL_INVALID_VALUE);} void LwjglGL11Bridge::color_pointer(int n,ArrayScalar t,int s,const void* p){if(!context_.arrays().color_pointer(n,t,s,p))set_error(GL_INVALID_VALUE);} void LwjglGL11Bridge::texcoord_pointer(int n,ArrayScalar t,int s,const void* p){if(!context_.arrays().texcoord_pointer(n,t,s,p))set_error(GL_INVALID_VALUE);} void LwjglGL11Bridge::normal_pointer(ArrayScalar t,int s,const void* p){if(!context_.arrays().normal_pointer(t,s,p))set_error(GL_INVALID_VALUE);}
void LwjglGL11Bridge::draw_arrays(std::uint32_t m,int first,int count){if(first<0||count<0){set_error(GL_INVALID_VALUE);return;}if(!context_.draw_arrays(m,first,count))set_error(GL_INVALID_OPERATION);}
void LwjglGL11Bridge::draw_elements(std::uint32_t m,int count,ArrayScalar t,const void* p){if(count<0||(!p&&count)){set_error(GL_INVALID_VALUE);return;}if(!context_.draw_elements(m,count,t,p))set_error(GL_INVALID_OPERATION);}
bool LwjglGL11Bridge::get_integer(std::uint32_t p,int* v,std::size_t n)const{return context_.core().get_integer(p,v,n);} bool LwjglGL11Bridge::get_float(std::uint32_t p,float* v,std::size_t n)const{return context_.core().get_float(p,v,n);} bool LwjglGL11Bridge::is_enabled(std::uint32_t c)const{return context_.core().is_enabled(c);}
std::uint32_t LwjglGL11Bridge::get_error(){auto e=error_;error_=GL_NO_ERROR;return e;}
const char* LwjglGL11Bridge::get_string(std::uint32_t n)const{switch(n){case GL_VENDOR:return "OptiCraft Switch";case GL_RENDERER:return "OptiCraft LegacyGL Software Backend";case GL_VERSION:return "1.1 OptiCraft compatibility";case GL_EXTENSIONS:return "";default:return nullptr;}}
bool LwjglGL11Bridge::read_pixels_rgba8(int x,int y,int w,int h,std::uint8_t* out,std::size_t bytes)const{
    if(!out||x<0||y<0||w<=0||h<=0)return false;
    const int framebuffer_width=context_.renderer().width();
    const int framebuffer_height=context_.renderer().height();
    // Subtraction-based bounds checks avoid signed overflow in x+w / y+h.
    if(x>framebuffer_width||y>framebuffer_height||
       w>framebuffer_width-x||h>framebuffer_height-y)return false;
    const auto row_bytes=static_cast<std::size_t>(w)*4u;
    if(row_bytes/4u!=static_cast<std::size_t>(w))return false;
    const auto rows=static_cast<std::size_t>(h);
    if(rows>std::numeric_limits<std::size_t>::max()/row_bytes)return false;
    const std::size_t need=row_bytes*rows;
    if(bytes<need)return false;
    const auto& src=context_.renderer().color_buffer();
    for(int row=0;row<h;row++)for(int col=0;col<w;col++){
        const auto source_index=static_cast<std::size_t>(y+row)*static_cast<std::size_t>(framebuffer_width)+static_cast<std::size_t>(x+col);
        const auto pixel=src[source_index];
        auto* destination=out+static_cast<std::size_t>(row)*row_bytes+static_cast<std::size_t>(col)*4u;
        destination[0]=static_cast<std::uint8_t>(pixel&0xffu);
        destination[1]=static_cast<std::uint8_t>((pixel>>8)&0xffu);
        destination[2]=static_cast<std::uint8_t>((pixel>>16)&0xffu);
        destination[3]=static_cast<std::uint8_t>((pixel>>24)&0xffu);
    }
    return true;
}
}
