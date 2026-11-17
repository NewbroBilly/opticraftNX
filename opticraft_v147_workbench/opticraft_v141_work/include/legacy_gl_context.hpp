#pragma once
#include "client_arrays.hpp"
#include "gl_compat_core.hpp"
#include "gl_texture_api.hpp"
#include "software_renderer.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace opti::gl {
// One CPU-side compatibility context joining fixed-function state, client arrays,
// texture storage/API and the software framebuffer.
class LegacyGLContext {
public:
    LegacyGLContext(int width,int height);
    CompatCore& core(){return core_;}
    const CompatCore& core()const{return core_;}
    ClientArrays& arrays(){return arrays_;}
    TextureApi& texture_api(){return texture_api_;}
    const TextureApi& texture_api()const{return texture_api_;}
    TextureStore& texture_store(){return texture_store_;}
    const SoftwareRenderer& renderer()const{return renderer_;}
    void resize(int width,int height);
    void clear(bool color=true,bool depth=true);
    void begin(Primitive mode);
    void color(float r,float g,float b,float a=1.0f);
    void texcoord(float u,float v);
    void normal(float x,float y,float z);
    void vertex(float x,float y,float z);
    bool end();
    bool draw_arrays(std::uint32_t mode,int first,int count);
    bool draw_elements(std::uint32_t mode,int count,ArrayScalar type,const void* indices);
    void submit(DrawBatch batch,bool already_transformed=false);
private:
    TextureStore texture_store_;
    TextureApi texture_api_;
    CompatCore core_;
    ClientArrays arrays_;
    SoftwareRenderer renderer_;
};
}
