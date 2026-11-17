#include "software_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace opti::gl {
namespace {
struct ScreenVertex { float x,y,z,r,g,b,a,uow,vow,qow; };
bool compare_value(std::uint32_t func,float a,float b) {
    switch(func) {
    case GL_NEVER_:return false; case GL_LESS_:return a<b; case GL_EQUAL_:return a==b;
    case GL_LEQUAL_:return a<=b; case GL_GREATER_:return a>b;
    case GL_NOTEQUAL_:return a!=b; case GL_GEQUAL_:return a>=b;
    case GL_ALWAYS_:return true; default:return false;
    }
}
float blend_factor(std::uint32_t f,int channel,const float* src,const float* dst) {
    switch(f) {
    case GL_ZERO_:return 0.0f; case GL_ONE_:return 1.0f;
    case GL_SRC_COLOR_:return src[channel]; case GL_ONE_MINUS_SRC_COLOR_:return 1.0f-src[channel];
    case GL_DST_COLOR_:return dst[channel]; case GL_ONE_MINUS_DST_COLOR_:return 1.0f-dst[channel];
    case GL_SRC_ALPHA_:return src[3]; case GL_ONE_MINUS_SRC_ALPHA_:return 1.0f-src[3];
    case GL_DST_ALPHA_:return dst[3]; case GL_ONE_MINUS_DST_ALPHA_:return 1.0f-dst[3];
    default:return 0.0f;
    }
}
std::uint32_t pack(float r,float g,float b,float a) {
    auto c=[](float x){return static_cast<std::uint32_t>(std::lround(std::clamp(x,0.0f,1.0f)*255.0f));};
    return c(r)|(c(g)<<8)|(c(b)<<16)|(c(a)<<24);
}
}
SoftwareRenderer::SoftwareRenderer(int w,int h):width_(w),height_(h) {
    if(w<=0||h<=0) throw std::invalid_argument("invalid renderer dimensions");
    color_.resize(static_cast<size_t>(w)*h);depth_.resize(static_cast<size_t>(w)*h,1.0f);
}
void SoftwareRenderer::resize(int w,int h) {
    if(w<=0||h<=0) throw std::invalid_argument("invalid renderer dimensions");
    width_=w;height_=h;color_.assign(static_cast<size_t>(w)*h,0);depth_.assign(static_cast<size_t>(w)*h,1.0f);
}
void SoftwareRenderer::clear(const RenderState& s,bool color,bool depth) {
    if(color) {
        const std::uint32_t value=pack(s.clear_color[0],s.clear_color[1],s.clear_color[2],s.clear_color[3]);
        std::uint32_t mask=0;
        for(int channel=0;channel<4;channel++) if(s.color_write_mask[static_cast<std::size_t>(channel)]) mask|=0xffu<<(channel*8);
        if(mask==0xffffffffu) std::fill(color_.begin(),color_.end(),value);
        else for(auto& pixel:color_) pixel=(pixel&~mask)|(value&mask);
    }
    if(depth) std::fill(depth_.begin(),depth_.end(),s.clear_depth);
}
void SoftwareRenderer::triangle(const Vertex& a,const Vertex& b,const Vertex& c,
                                const RenderState& state,const TextureStore* textures,
                                std::uint32_t texture_id) {
    auto project=[&](const Vertex& v)->ScreenVertex {
        const auto& p=v.clip_position;
        const float iw=(std::fabs(p.w)>1e-8f)?1.0f/p.w:0.0f;
        const float texture_q=(std::fabs(v.texcoord.w)>1e-8f)?v.texcoord.w:0.0f;
        const float nx=p.x*iw,ny=p.y*iw,nz=p.z*iw;
        return {
            state.viewport.x+(nx*0.5f+0.5f)*state.viewport.width,
            state.viewport.y+(1.0f-(ny*0.5f+0.5f))*state.viewport.height,
            nz*0.5f+0.5f,
            v.color.x,v.color.y,v.color.z,v.color.w,
            v.texcoord.x*iw,v.texcoord.y*iw,texture_q*iw
        };
    };
    const ScreenVertex v0=project(a),v1=project(b),v2=project(c);
    const double area=(static_cast<double>(v1.x)-v0.x)*(static_cast<double>(v2.y)-v0.y)-
                      (static_cast<double>(v1.y)-v0.y)*(static_cast<double>(v2.x)-v0.x);
    if(std::fabs(area)<1e-7) return;
    const int minx=std::max({0,state.viewport.x,static_cast<int>(std::floor(std::min({v0.x,v1.x,v2.x})))});
    const int maxx=std::min({width_-1,state.viewport.x+state.viewport.width-1,static_cast<int>(std::ceil(std::max({v0.x,v1.x,v2.x})))});
    const int miny=std::max({0,state.viewport.y,static_cast<int>(std::floor(std::min({v0.y,v1.y,v2.y})))});
    const int maxy=std::min({height_-1,state.viewport.y+state.viewport.height-1,static_cast<int>(std::ceil(std::max({v0.y,v1.y,v2.y})))});
    const bool use_texture=state.texture_2d && textures && texture_id!=0 &&
                           textures->get(texture_id) && textures->get(texture_id)->complete();
    const std::uint32_t color_write_mask=(state.color_write_mask[0]?0x000000ffu:0u)|(state.color_write_mask[1]?0x0000ff00u:0u)|(state.color_write_mask[2]?0x00ff0000u:0u)|(state.color_write_mask[3]?0xff000000u:0u);
    const bool positive_area=area>0.0f;
    auto covered_edge=[&](double edge,const ScreenVertex& from,const ScreenVertex& to) {
        if(!positive_area) edge=-edge;
        if(edge>0.0) return true;
        if(edge<0.0) return false;
        const double dx=positive_area?static_cast<double>(to.x)-from.x:
                                      static_cast<double>(from.x)-to.x;
        const double dy=positive_area?static_cast<double>(to.y)-from.y:
                                      static_cast<double>(from.y)-to.y;
        return dy<0.0 || (dy==0.0 && dx>0.0);
    };
    for(int y=miny;y<=maxy;y++) for(int x=minx;x<=maxx;x++) {
        const double px=static_cast<double>(x)+0.5,py=static_cast<double>(y)+0.5;
        const double e0=(static_cast<double>(v2.x)-v1.x)*(py-v1.y)-
                         (static_cast<double>(v2.y)-v1.y)*(px-v1.x);
        const double e1=(static_cast<double>(v0.x)-v2.x)*(py-v2.y)-
                         (static_cast<double>(v0.y)-v2.y)*(px-v2.x);
        const double e2=(static_cast<double>(v1.x)-v0.x)*(py-v0.y)-
                         (static_cast<double>(v1.y)-v0.y)*(px-v0.x);
        if(!covered_edge(e0,v1,v2)||!covered_edge(e1,v2,v0)||
           !covered_edge(e2,v0,v1)) continue;
        const float w0=static_cast<float>(e0/area),w1=static_cast<float>(e1/area),
                    w2=static_cast<float>(e2/area);
        const float z=w0*v0.z+w1*v1.z+w2*v2.z;
        const size_t idx=static_cast<size_t>(y)*width_+x;
        if(state.depth_test && !compare_value(state.depth_func,z,depth_[idx])) continue;
        float r=w0*v0.r+w1*v1.r+w2*v2.r;
        float g=w0*v0.g+w1*v1.g+w2*v2.g;
        float bl=w0*v0.b+w1*v1.b+w2*v2.b;
        float al=w0*v0.a+w1*v1.a+w2*v2.a;
        if(use_texture) {
            const float qow=w0*v0.qow+w1*v1.qow+w2*v2.qow;
            if(std::fabs(qow)>1e-8f) {
                const float u=(w0*v0.uow+w1*v1.uow+w2*v2.uow)/qow;
                const float v=(w0*v0.vow+w1*v1.vow+w2*v2.vow)/qow;
                const std::uint32_t tex=textures->sample(texture_id,u,v);
                r*=float(tex&255u)/255.0f;
                g*=float((tex>>8)&255u)/255.0f;
                bl*=float((tex>>16)&255u)/255.0f;
                al*=float((tex>>24)&255u)/255.0f;
            }
        }
        if(state.alpha_test && !compare_value(state.alpha_func,al,state.alpha_ref)) continue;
        if(state.blend) {
            const std::uint32_t dstp=color_[idx];
            const float src[4]={r,g,bl,al};
            const float dst[4]={float(dstp&255u)/255.0f,float((dstp>>8)&255u)/255.0f,
                                float((dstp>>16)&255u)/255.0f,float((dstp>>24)&255u)/255.0f};
            float out[4];
            for(int k=0;k<4;k++)
                out[k]=src[k]*blend_factor(state.blend_src,k,src,dst)+
                       dst[k]*blend_factor(state.blend_dst,k,src,dst);
            r=out[0];g=out[1];bl=out[2];al=out[3];
        }
        const std::uint32_t output=pack(r,g,bl,al);
        color_[idx]=(color_[idx]&~color_write_mask)|(output&color_write_mask);
        if(state.depth_test) depth_[idx]=z;
    }
}
void SoftwareRenderer::draw_impl(const DrawBatch& batch,const RenderState& state,
                                 const TextureStore* textures,std::uint32_t texture_id) {
    for(size_t i=0;i+2<batch.indices.size();i+=3) {
        const auto a=batch.indices[i],b=batch.indices[i+1],c=batch.indices[i+2];
        if(a<batch.vertices.size()&&b<batch.vertices.size()&&c<batch.vertices.size())
            triangle(batch.vertices[a],batch.vertices[b],batch.vertices[c],state,textures,texture_id);
    }
}
void SoftwareRenderer::draw(const DrawBatch& batch,const RenderState& state) {
    draw_impl(batch,state,nullptr,0);
}
void SoftwareRenderer::draw(const DrawBatch& batch,const RenderState& state,
                            const TextureStore& textures,std::uint32_t texture_id) {
    draw_impl(batch,state,&textures,texture_id);
}
}
