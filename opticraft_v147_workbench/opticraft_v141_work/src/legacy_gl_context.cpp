#include "legacy_gl_context.hpp"
#include <utility>
namespace opti::gl {
LegacyGLContext::LegacyGLContext(int w,int h)
    :texture_api_(texture_store_),renderer_(w,h) {
    core_.viewport(0,0,w,h);
}
void LegacyGLContext::resize(int w,int h) {
    renderer_.resize(w,h);
    core_.viewport(0,0,w,h);
}
void LegacyGLContext::clear(bool color,bool depth) {
    renderer_.clear(core_.state(),color,depth);
}
void LegacyGLContext::begin(Primitive mode){core_.begin(mode);}
void LegacyGLContext::color(float r,float g,float b,float a){core_.color(r,g,b,a);}
void LegacyGLContext::texcoord(float u,float v){core_.texcoord(u,v);}
void LegacyGLContext::normal(float x,float y,float z){core_.normal(x,y,z);}
void LegacyGLContext::vertex(float x,float y,float z){core_.vertex(x,y,z);}
bool LegacyGLContext::end() {
    DrawBatch batch=core_.end();
    if(batch.indices.empty())return true;
    submit(std::move(batch),true);
    return true;
}
bool LegacyGLContext::draw_arrays(std::uint32_t mode,int first,int count) {
    DrawBatch batch;
    if(!arrays_.draw_arrays(mode,first,count,batch))return false;
    submit(std::move(batch),false);
    return true;
}
bool LegacyGLContext::draw_elements(std::uint32_t mode,int count,ArrayScalar type,const void* indices) {
    DrawBatch batch;
    if(!arrays_.draw_elements(mode,count,type,indices,batch))return false;
    submit(std::move(batch),false);
    return true;
}
void LegacyGLContext::submit(DrawBatch batch,bool already_transformed) {
    if(!already_transformed)core_.transform(batch);
    renderer_.draw(batch,core_.state(),texture_store_,texture_api_.bound_2d());
}
}
