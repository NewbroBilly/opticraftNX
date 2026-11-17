#include "gl_compat_core.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace opti::gl {
namespace {
constexpr std::uint32_t GL_MODELVIEW=0x1700, GL_PROJECTION=0x1701, GL_TEXTURE=0x1702;
constexpr std::uint32_t GL_DEPTH_TEST=0x0B71, GL_BLEND=0x0BE2, GL_CULL_FACE=0x0B44,
 GL_TEXTURE_2D=0x0DE1, GL_FOG=0x0B60, GL_LIGHTING=0x0B50, GL_ALPHA_TEST=0x0BC0;
}
std::array<float,16> CompatCore::identity() {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}
std::array<float,16> CompatCore::multiply(const std::array<float,16>& a,
                                          const std::array<float,16>& b) {
    std::array<float,16> o{};
    for(int c=0;c<4;c++) for(int r=0;r<4;r++)
        for(int k=0;k<4;k++) o[c*4+r]+=a[k*4+r]*b[c*4+k];
    return o;
}
CompatCore::CompatCore() {
    modelview_.push_back(identity()); projection_.push_back(identity()); texture_.push_back(identity());
}
std::vector<std::array<float,16>>& CompatCore::stack() {
    if(target_==MODELVIEW) return modelview_;
    if(target_==PROJECTION) return projection_;
    return texture_;
}
void CompatCore::matrix_mode(std::uint32_t mode) {
    if(in_begin_) throw std::logic_error("matrix_mode inside begin/end");
    if(mode==GL_MODELVIEW) target_=MODELVIEW;
    else if(mode==GL_PROJECTION) target_=PROJECTION;
    else if(mode==GL_TEXTURE) target_=TEXTURE;
    else throw std::invalid_argument("unsupported matrix mode");
}
void CompatCore::load_identity() {
    if(in_begin_) throw std::logic_error("load_identity inside begin/end");
    stack().back()=identity();
}
void CompatCore::load_matrix(const std::array<float,16>& values) {
    if(in_begin_) throw std::logic_error("load_matrix inside begin/end");
    stack().back()=values;
}
void CompatCore::mult_matrix(const std::array<float,16>& values) {
    if(in_begin_) throw std::logic_error("mult_matrix inside begin/end");
    stack().back()=multiply(stack().back(),values);
}
void CompatCore::push_matrix() {
    if(in_begin_) throw std::logic_error("push_matrix inside begin/end");
    auto& s=stack(); if(s.size()>=32) throw std::overflow_error("matrix stack overflow");
    s.push_back(s.back());
}
void CompatCore::pop_matrix() {
    if(in_begin_) throw std::logic_error("pop_matrix inside begin/end");
    auto& s=stack(); if(s.size()<=1) throw std::underflow_error("matrix stack underflow");
    s.pop_back();
}
void CompatCore::translate(float x,float y,float z) {
    auto m=identity(); m[12]=x;m[13]=y;m[14]=z; stack().back()=multiply(stack().back(),m);
}
void CompatCore::scale(float x,float y,float z) {
    auto m=identity();m[0]=x;m[5]=y;m[10]=z;stack().back()=multiply(stack().back(),m);
}
void CompatCore::rotate(float deg,float x,float y,float z) {
    const float len=std::sqrt(x*x+y*y+z*z); if(len==0) return;
    x/=len;y/=len;z/=len;
    const float a=deg*0.01745329251994329577f,c=std::cos(a),s=std::sin(a),t=1-c;
    auto m=identity();
    m[0]=t*x*x+c; m[4]=t*x*y-s*z; m[8]=t*x*z+s*y;
    m[1]=t*x*y+s*z; m[5]=t*y*y+c; m[9]=t*y*z-s*x;
    m[2]=t*x*z-s*y; m[6]=t*y*z+s*x; m[10]=t*z*z+c;
    stack().back()=multiply(stack().back(),m);
}
void CompatCore::ortho(double l,double r,double b,double t,double n,double f) {
    if(r==l||t==b||f==n) throw std::invalid_argument("invalid ortho volume");
    auto m=identity();
    m[0]=float(2/(r-l));m[5]=float(2/(t-b));m[10]=float(-2/(f-n));
    m[12]=float(-(r+l)/(r-l));m[13]=float(-(t+b)/(t-b));m[14]=float(-(f+n)/(f-n));
    stack().back()=multiply(stack().back(),m);
}
void CompatCore::begin(Primitive p) {
    if(in_begin_) throw std::logic_error("nested begin");
    in_begin_=true;primitive_=p;pending_.clear();
}
void CompatCore::color(float r,float g,float b,float a) { current_.color={r,g,b,a}; }
void CompatCore::texcoord(float u,float v) { current_.texcoord={u,v,0,1}; }
void CompatCore::normal(float x,float y,float z) { current_.normal={x,y,z,0}; }
void CompatCore::append_vertex(Vertex v) {
    if(!in_begin_) throw std::logic_error("vertex outside begin/end");
    pending_.push_back(v);
}
void CompatCore::vertex(float x,float y,float z) {
    current_.position={x,y,z,1};append_vertex(current_);
}
DrawBatch CompatCore::end() {
    if(!in_begin_) throw std::logic_error("end without begin");
    in_begin_=false;DrawBatch out;out.source_mode=primitive_;out.vertices=pending_;
    const std::uint32_t n=static_cast<std::uint32_t>(out.vertices.size());
    auto tri=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c){
        out.indices.push_back(a);out.indices.push_back(b);out.indices.push_back(c);};
    switch(primitive_) {
    case POINTS: break;
    case LINES: for(std::uint32_t i=0;i+1<n;i+=2){out.indices.push_back(i);out.indices.push_back(i+1);} break;
    case LINE_STRIP: for(std::uint32_t i=0;i+1<n;i++){out.indices.push_back(i);out.indices.push_back(i+1);} break;
    case LINE_LOOP: if(n>1){for(std::uint32_t i=0;i<n;i++){out.indices.push_back(i);out.indices.push_back((i+1)%n);}} break;
    case TRIANGLES: for(std::uint32_t i=0;i+2<n;i+=3)tri(i,i+1,i+2);break;
    case TRIANGLE_STRIP: for(std::uint32_t i=0;i+2<n;i++) (i&1)?tri(i+1,i,i+2):tri(i,i+1,i+2);break;
    case TRIANGLE_FAN: for(std::uint32_t i=1;i+1<n;i++)tri(0,i,i+1);break;
    case QUADS: for(std::uint32_t i=0;i+3<n;i+=4){tri(i,i+1,i+2);tri(i,i+2,i+3);}break;
    }
    // Fixed-function transform: projection * modelview * vertex.
    transform(out);
    pending_.clear();return out;
}
void CompatCore::transform(DrawBatch& batch) const {
    const auto combined = multiply(projection_.back(), modelview_.back());
    const auto& texture = texture_.back();
    for (auto& v : batch.vertices) {
        const float in[4]={v.position.x,v.position.y,v.position.z,v.position.w};
        float q[4]={0,0,0,0};
        for(int row=0;row<4;row++)
            for(int col=0;col<4;col++) q[row]+=combined[col*4+row]*in[col];
        v.clip_position={q[0],q[1],q[2],q[3]};

        const float tex_in[4]={v.texcoord.x,v.texcoord.y,v.texcoord.z,v.texcoord.w};
        float tex_out[4]={0,0,0,0};
        for(int row=0;row<4;row++)
            for(int col=0;col<4;col++) tex_out[row]+=texture[col*4+row]*tex_in[col];
        v.texcoord={tex_out[0],tex_out[1],tex_out[2],tex_out[3]};
    }
}
void CompatCore::enable(std::uint32_t cap,bool v) {
    switch(cap){case GL_DEPTH_TEST:state_.depth_test=v;break;case GL_BLEND:state_.blend=v;break;
    case GL_CULL_FACE:state_.cull=v;break;case GL_TEXTURE_2D:state_.texture_2d=v;break;
    case GL_FOG:state_.fog=v;break;case GL_LIGHTING:state_.lighting=v;break;
    case GL_ALPHA_TEST:state_.alpha_test=v;break;default:break;}
}
void CompatCore::viewport(int x,int y,int w,int h){state_.viewport={x,y,std::max(0,w),std::max(0,h)};}
void CompatCore::clear_color(float r,float g,float b,float a){state_.clear_color[0]=r;state_.clear_color[1]=g;state_.clear_color[2]=b;state_.clear_color[3]=a;}
void CompatCore::clear_depth(float d){state_.clear_depth=std::clamp(d,0.0f,1.0f);}
void CompatCore::depth_func(std::uint32_t f){
    if(f<GL_NEVER_||f>GL_ALWAYS_) throw std::invalid_argument("unsupported depth function");
    state_.depth_func=f;
}
void CompatCore::blend_func(std::uint32_t s,std::uint32_t d){
    auto valid=[](std::uint32_t f){
        return f==GL_ZERO_||f==GL_ONE_||f==GL_SRC_COLOR_||f==GL_ONE_MINUS_SRC_COLOR_||
               f==GL_DST_COLOR_||f==GL_ONE_MINUS_DST_COLOR_||f==GL_SRC_ALPHA_||
               f==GL_ONE_MINUS_SRC_ALPHA_||f==GL_DST_ALPHA_||f==GL_ONE_MINUS_DST_ALPHA_;
    };
    if(!valid(s)||!valid(d)) throw std::invalid_argument("unsupported blend factor");
    state_.blend_src=s;state_.blend_dst=d;
}
void CompatCore::alpha_func(std::uint32_t f,float ref){
    if(f<GL_NEVER_||f>GL_ALWAYS_) throw std::invalid_argument("unsupported alpha function");
    state_.alpha_func=f;state_.alpha_ref=std::clamp(ref,0.0f,1.0f);
}
void CompatCore::color_mask(bool red,bool green,bool blue,bool alpha) {
    state_.color_write_mask={red,green,blue,alpha};
}

bool CompatCore::is_enabled(std::uint32_t cap) const {
    switch(cap){
    case GL_DEPTH_TEST:return state_.depth_test;
    case GL_BLEND:return state_.blend;
    case GL_CULL_FACE:return state_.cull;
    case GL_TEXTURE_2D:return state_.texture_2d;
    case GL_FOG:return state_.fog;
    case GL_LIGHTING:return state_.lighting;
    case GL_ALPHA_TEST:return state_.alpha_test;
    default:return false;
    }
}
bool CompatCore::get_integer(std::uint32_t pname,int* out,std::size_t count) const {
    if(!out)return false;
    switch(pname){
    case GL_VIEWPORT_:
        if(count<4)return false;
        out[0]=state_.viewport.x;out[1]=state_.viewport.y;
        out[2]=state_.viewport.width;out[3]=state_.viewport.height;return true;
    case GL_DEPTH_FUNC_:
        if(count<1)return false;
        out[0]=static_cast<int>(state_.depth_func);return true;
    case GL_BLEND_SRC_:
        if(count<1)return false;
        out[0]=static_cast<int>(state_.blend_src);return true;
    case GL_BLEND_DST_:
        if(count<1)return false;
        out[0]=static_cast<int>(state_.blend_dst);return true;
    case GL_ALPHA_TEST_FUNC_:
        if(count<1)return false;
        out[0]=static_cast<int>(state_.alpha_func);return true;
    default:return false;
    }
}
bool CompatCore::get_float(std::uint32_t pname,float* out,std::size_t count) const {
    if(!out)return false;
    if(pname==GL_ALPHA_TEST_REF_&&count>=1){out[0]=state_.alpha_ref;return true;}
    return false;
}
}
