#pragma once
#include "gl_compat_core.hpp"
#include "texture_store.hpp"
#include <cstdint>
#include <vector>

namespace opti::gl {
class SoftwareRenderer {
public:
    SoftwareRenderer(int width,int height);
    void resize(int width,int height);
    void clear(const RenderState& state, bool color=true, bool depth=true);
    void draw(const DrawBatch& batch,const RenderState& state);
    // CPU reference path for GL_TEXTURE_2D. texture_id is the currently bound object.
    void draw(const DrawBatch& batch,const RenderState& state,
              const TextureStore& textures,std::uint32_t texture_id);
    const std::vector<std::uint32_t>& color_buffer() const { return color_; }
    const std::vector<float>& depth_buffer() const { return depth_; }
    int width() const { return width_; }
    int height() const { return height_; }
private:
    int width_,height_;
    std::vector<std::uint32_t> color_;
    std::vector<float> depth_;
    void triangle(const Vertex&,const Vertex&,const Vertex&,const RenderState&,
                  const TextureStore*,std::uint32_t);
    void draw_impl(const DrawBatch&,const RenderState&,const TextureStore*,std::uint32_t);
};
}
