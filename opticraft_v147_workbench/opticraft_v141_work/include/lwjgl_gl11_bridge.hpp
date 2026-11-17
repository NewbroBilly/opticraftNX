#pragma once
#include "legacy_gl_context.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace opti::gl {
// Host-side LWJGL 2 / OpenGL 1.1 compatibility facade. JNI wrappers can forward
// their decoded arguments here; all calls share one LegacyGLContext.
class LwjglGL11Bridge {
public:
    explicit LwjglGL11Bridge(int width=960,int height=544);
    LegacyGLContext& context(){return context_;}
    const LegacyGLContext& context()const{return context_;}
    void set_present_callback(std::function<void(const std::uint32_t*,int,int)> cb){present_=std::move(cb);}
    void resize(int w,int h);
    void display_swap_buffers();
    void display_update(){display_swap_buffers();}

    void begin(std::uint32_t mode); void end();
    void color3f(float r,float g,float b); void color4f(float r,float g,float b,float a);
    void tex_coord2f(float u,float v); void normal3f(float x,float y,float z);
    void vertex3f(float x,float y,float z);
    void matrix_mode(std::uint32_t); void load_identity(); void load_matrix(const std::array<float,16>&); void mult_matrix(const std::array<float,16>&); void push_matrix(); void pop_matrix();
    void translatef(float,float,float); void scalef(float,float,float); void rotatef(float,float,float,float);
    void ortho(double,double,double,double,double,double);
    void viewport(int,int,int,int); void clear_color(float,float,float,float); void clear_depth(double);
    void clear(std::uint32_t mask); void enable(std::uint32_t); void disable(std::uint32_t);
    void depth_func(std::uint32_t); void blend_func(std::uint32_t,std::uint32_t); void alpha_func(std::uint32_t,float);
    void color_mask(bool,bool,bool,bool);
    void pixel_store_i(std::uint32_t,int);
    std::vector<std::uint32_t> gen_textures(std::size_t); void delete_textures(const std::uint32_t*,std::size_t);
    void bind_texture(std::uint32_t,std::uint32_t); void tex_parameter_i(std::uint32_t,std::uint32_t,int);
    void tex_image_2d(std::uint32_t,int,int,int,int,int,std::uint32_t,std::uint32_t,const std::uint8_t*,std::size_t);
    void tex_sub_image_2d(std::uint32_t,int,int,int,int,int,std::uint32_t,std::uint32_t,const std::uint8_t*,std::size_t);
    void enable_client_state(std::uint32_t); void disable_client_state(std::uint32_t);
    bool set_client_array_bytes(std::uint32_t array,int components,ArrayScalar scalar,int stride,const std::uint8_t* bytes,std::size_t size);
    void vertex_pointer(int,ArrayScalar,int,const void*); void color_pointer(int,ArrayScalar,int,const void*);
    void texcoord_pointer(int,ArrayScalar,int,const void*); void normal_pointer(ArrayScalar,int,const void*);
    void draw_arrays(std::uint32_t,int,int); void draw_elements(std::uint32_t,int,ArrayScalar,const void*);
    bool get_integer(std::uint32_t,int*,std::size_t)const; bool get_float(std::uint32_t,float*,std::size_t)const;
    bool is_enabled(std::uint32_t)const; std::uint32_t get_error();
    const char* get_string(std::uint32_t)const;
    bool read_pixels_rgba8(int x,int y,int w,int h,std::uint8_t* out,std::size_t bytes)const;
    const std::vector<std::uint32_t>& framebuffer()const{return context_.renderer().color_buffer();}
private:
    LegacyGLContext context_;
    std::function<void(const std::uint32_t*,int,int)> present_;
    std::array<std::vector<std::uint8_t>,4> client_array_storage_{};
    std::uint32_t error_=0;
    bool valid_dimensions(int,int)const;
    void set_error(std::uint32_t e){if(error_==0)error_=e;}
    static bool mode(std::uint32_t,Primitive&);
};
}
