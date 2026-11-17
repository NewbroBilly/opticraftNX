#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace opti::gl {
enum Primitive : std::uint32_t {
    POINTS=0, LINES=1, LINE_LOOP=2, LINE_STRIP=3,
    TRIANGLES=4, TRIANGLE_STRIP=5, TRIANGLE_FAN=6, QUADS=7
};
struct Vec4 { float x=0, y=0, z=0, w=1; };
struct Vertex {
    Vec4 position, color, texcoord, normal;
    Vec4 clip_position;
};
struct DrawBatch {
    Primitive source_mode=TRIANGLES;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};
struct Viewport { int x=0,y=0,width=960,height=544; };
constexpr std::uint32_t GL_NEVER_=0x0200, GL_LESS_=0x0201, GL_EQUAL_=0x0202,
 GL_LEQUAL_=0x0203, GL_GREATER_=0x0204, GL_NOTEQUAL_=0x0205,
 GL_GEQUAL_=0x0206, GL_ALWAYS_=0x0207;
constexpr std::uint32_t GL_ZERO_=0, GL_ONE_=1, GL_SRC_COLOR_=0x0300,
 GL_ONE_MINUS_SRC_COLOR_=0x0301, GL_SRC_ALPHA_=0x0302,
 GL_ONE_MINUS_SRC_ALPHA_=0x0303, GL_DST_ALPHA_=0x0304,
 GL_ONE_MINUS_DST_ALPHA_=0x0305, GL_DST_COLOR_=0x0306,
 GL_ONE_MINUS_DST_COLOR_=0x0307;
constexpr std::uint32_t GL_VIEWPORT_=0x0BA2, GL_DEPTH_FUNC_=0x0B74,
 GL_BLEND_SRC_=0x0BE1, GL_BLEND_DST_=0x0BE0,
 GL_ALPHA_TEST_FUNC_=0x0BC1, GL_ALPHA_TEST_REF_=0x0BC2;
struct RenderState {
    bool depth_test=false, blend=false, cull=false, texture_2d=false,
         fog=false, lighting=false, alpha_test=false;
    std::array<bool,4> color_write_mask={true,true,true,true};
    std::uint32_t depth_func=0x0201; // GL_LESS
    std::uint32_t blend_src=GL_ONE_, blend_dst=GL_ZERO_;
    std::uint32_t alpha_func=GL_ALWAYS_;
    float alpha_ref=0.0f;
    float clear_color[4]={0,0,0,0};
    float clear_depth=1.0f;
    Viewport viewport{};
};

class CompatCore {
public:
    CompatCore();
    void matrix_mode(std::uint32_t mode);
    void load_identity();
    void load_matrix(const std::array<float,16>& values);
    void mult_matrix(const std::array<float,16>& values);
    void push_matrix();
    void pop_matrix();
    void translate(float x,float y,float z);
    void rotate(float degrees,float x,float y,float z);
    void scale(float x,float y,float z);
    void ortho(double l,double r,double b,double t,double n,double f);
    void begin(Primitive mode);
    void color(float r,float g,float b,float a);
    void texcoord(float u,float v);
    void normal(float x,float y,float z);
    void vertex(float x,float y,float z);
    DrawBatch end();
    void transform(DrawBatch& batch) const;
    void enable(std::uint32_t cap, bool value);
    void viewport(int x,int y,int w,int h);
    void clear_color(float r,float g,float b,float a);
    void clear_depth(float d);
    void depth_func(std::uint32_t func);
    void blend_func(std::uint32_t src,std::uint32_t dst);
    void color_mask(bool red,bool green,bool blue,bool alpha);
    void alpha_func(std::uint32_t func,float reference);
    bool is_enabled(std::uint32_t capability) const;
    bool get_integer(std::uint32_t pname,int* values,std::size_t count) const;
    bool get_float(std::uint32_t pname,float* values,std::size_t count) const;
    const RenderState& state() const { return state_; }
    const std::array<float,16>& modelview() const { return modelview_.back(); }
    const std::array<float,16>& projection() const { return projection_.back(); }
    bool in_begin() const { return in_begin_; }
private:
    enum MatrixTarget { MODELVIEW, PROJECTION, TEXTURE };
    MatrixTarget target_=MODELVIEW;
    std::vector<std::array<float,16>> modelview_, projection_, texture_;
    RenderState state_{};
    bool in_begin_=false;
    Primitive primitive_=TRIANGLES;
    Vertex current_{};
    std::vector<Vertex> pending_;
    static std::array<float,16> identity();
    static std::array<float,16> multiply(const std::array<float,16>&,
                                         const std::array<float,16>&);
    std::vector<std::array<float,16>>& stack();
    void append_vertex(Vertex v);
};
}
