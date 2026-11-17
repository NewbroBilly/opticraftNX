#include "software_renderer.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace opti::gl;

int main() {
    TextureStore textures;
    assert(textures.create(7));
    const std::uint8_t texels[] = {
        255,0,0,255, 0,255,0,255,
        0,0,255,255, 255,255,255,255
    };
    assert(textures.upload(7,2,2,PixelFormat::RGBA,texels,sizeof(texels),1));
    assert(textures.set_filter(7,true,TextureFilter::Nearest));
    assert(textures.set_filter(7,false,TextureFilter::Nearest));
    assert(textures.set_wrap(7,true,TextureWrap::Clamp));
    assert(textures.set_wrap(7,false,TextureWrap::Clamp));
    assert(textures.set_filter(7,true,TextureFilter::Linear));
    assert(textures.set_filter(7,false,TextureFilter::Linear));
    assert(textures.sample(7,0.25f,0.25f)==0xFF0000FFu);
    assert(textures.sample(7,0.75f,0.25f)==0xFF00FF00u);
    assert(textures.sample(7,0.25f,0.75f)==0xFFFF0000u);
    assert(textures.sample(7,0.0f,0.0f)==0xFF0000FFu);
    assert(textures.set_filter(7,true,TextureFilter::Nearest));
    assert(textures.set_filter(7,false,TextureFilter::Nearest));

    DrawBatch batch;
    batch.indices={0,1,2};
    Vertex a,b,c;
    a.clip_position={-1,-1,0,1}; a.texcoord={0,0,0,1};
    b.clip_position={ 1,-1,0,1}; b.texcoord={1,0,0,1};
    c.clip_position={-1, 1,0,1}; c.texcoord={0,1,0,1};
    a.color=b.color=c.color={1,1,1,1};
    batch.vertices={a,b,c};

    RenderState state;
    state.viewport={0,0,8,8};
    state.texture_2d=true;
    SoftwareRenderer renderer(8,8);
    renderer.clear(state);
    renderer.draw(batch,state,textures,7);

    // The triangle covers the lower-left half; sample interior pixels near each corner.
    const auto& pixels=renderer.color_buffer();
    const auto blue=pixels[2*8+1];
    const auto green=pixels[7*8+6];
    const auto red=pixels[6*8+1];
    assert(((blue>>16)&255u)>200u && (blue&255u)<30u);
    assert(((green>>8)&255u)>150u && (green&255u)<30u);
    assert((red&255u)>150u && ((red>>8)&255u)<30u);

    // Legacy untextured overload remains available and does not sample a texture.
    state.texture_2d=false;
    renderer.clear(state);
    renderer.draw(batch,state);
    assert(renderer.color_buffer()[2*8+1]==0xFFFFFFFFu);

    TextureStore translucent;
    assert(translucent.create(9));
    const std::uint8_t half_alpha_red[]={255,0,0,128};
    assert(translucent.upload(9,1,1,PixelFormat::RGBA,half_alpha_red,
                              sizeof(half_alpha_red),1));
    assert(translucent.set_filter(9,true,TextureFilter::Nearest));
    assert(translucent.set_filter(9,false,TextureFilter::Nearest));
    DrawBatch quad;
    quad.indices={0,1,2,0,2,3};
    Vertex q0,q1,q2,q3;
    q0.clip_position={-0.5f, 0.5f,0,1}; q0.texcoord={0.5f,0.5f,0,1};
    q1.clip_position={ 0.5f, 0.5f,0,1}; q1.texcoord={0.5f,0.5f,0,1};
    q2.clip_position={ 0.5f,-0.5f,0,1}; q2.texcoord={0.5f,0.5f,0,1};
    q3.clip_position={-0.5f,-0.5f,0,1}; q3.texcoord={0.5f,0.5f,0,1};
    q0.color=q1.color=q2.color=q3.color={1,1,1,1};
    quad.vertices={q0,q1,q2,q3};
    RenderState blended;
    blended.viewport={0,0,8,8};
    blended.texture_2d=true;
    blended.blend=true;
    blended.blend_src=GL_SRC_ALPHA_;
    blended.blend_dst=GL_ONE_MINUS_SRC_ALPHA_;
    SoftwareRenderer blended_renderer(8,8);
    blended_renderer.clear(blended);
    blended_renderer.draw(quad,blended,translucent,9);
    for(int y=2;y<6;y++) for(int x=2;x<6;x++) {
        const auto pixel=blended_renderer.color_buffer()[static_cast<std::size_t>(y)*8+x];
        assert((pixel&255u)==128u);
    }
    quad.indices={0,2,1,0,3,2};
    blended_renderer.clear(blended);
    blended_renderer.draw(quad,blended,translucent,9);
    for(int y=2;y<6;y++) for(int x=2;x<6;x++) {
        const auto pixel=blended_renderer.color_buffer()[static_cast<std::size_t>(y)*8+x];
        assert((pixel&255u)==128u);
    }
    RenderState masked=blended;
    blended_renderer.clear(masked);
    masked.color_write_mask={true,false,false,false};
    blended_renderer.draw(quad,masked,translucent,9);
    const auto masked_pixel=blended_renderer.color_buffer()[3*8+3];
    assert((masked_pixel&255u)==128u && ((masked_pixel>>8)&255u)==0u &&
           ((masked_pixel>>16)&255u)==0u && ((masked_pixel>>24)&255u)==0u);
    std::cout<<"software_renderer_texture_test: PASS\\n";
}
