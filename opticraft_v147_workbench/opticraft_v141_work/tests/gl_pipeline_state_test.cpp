#include "gl_compat_core.hpp"
#include "software_renderer.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace opti::gl;

static DrawBatch triangle(float r,float g,float b,float a,float z=0.0f) {
    DrawBatch batch; batch.indices={0,1,2};
    Vertex v0,v1,v2;
    v0.position={-1,-1,z,1}; v1.position={1,-1,z,1}; v2.position={-1,1,z,1};
    v0.clip_position=v0.position; v1.clip_position=v1.position; v2.clip_position=v2.position;
    v0.color=v1.color=v2.color={r,g,b,a};
    batch.vertices={v0,v1,v2};
    return batch;
}
int main() {
    CompatCore core;
    core.enable(0x0BE2,true);
    core.blend_func(GL_SRC_ALPHA_,GL_ONE_MINUS_SRC_ALPHA_);
    core.alpha_func(GL_GREATER_,0.4f);
    core.enable(0x0BC0,true);
    assert(core.is_enabled(0x0BE2));
    assert(core.is_enabled(0x0BC0));
    int ints[4]={};
    assert(core.get_integer(GL_VIEWPORT_,ints,4));
    assert(ints[2]==960 && ints[3]==544);
    assert(core.get_integer(GL_BLEND_SRC_,ints,1) && ints[0]==int(GL_SRC_ALPHA_));
    float floats[1]={};
    assert(core.get_float(GL_ALPHA_TEST_REF_,floats,1) && std::fabs(floats[0]-0.4f)<1e-6f);
    assert(!core.get_integer(GL_VIEWPORT_,ints,3));
    assert(!core.get_float(GL_ALPHA_TEST_REF_,floats,0));

    RenderState state;
    state.viewport={0,0,4,4};
    state.clear_color[0]=0;state.clear_color[1]=0;state.clear_color[2]=1;state.clear_color[3]=1;
    state.blend=true;state.blend_src=GL_SRC_ALPHA_;state.blend_dst=GL_ONE_MINUS_SRC_ALPHA_;
    state.alpha_test=true;state.alpha_func=GL_GREATER_;state.alpha_ref=0.4f;
    SoftwareRenderer renderer(4,4);
    renderer.clear(state);
    renderer.draw(triangle(1,0,0,0.5f),state);
    const auto mixed=renderer.color_buffer()[1*4+1];
    assert(((mixed&255u)>=126u)&&((mixed&255u)<=129u));
    assert(((mixed>>16)&255u)>=126u && ((mixed>>16)&255u)<=129u);

    // Alpha test rejects fragments below the reference and preserves the clear color.
    renderer.clear(state);
    state.alpha_ref=0.6f;
    renderer.draw(triangle(1,0,0,0.5f),state);
    const auto rejected=renderer.color_buffer()[1*4+1];
    assert(((rejected>>16)&255u)==255u && (rejected&255u)==0u);

    // Depth comparisons are selectable; GREATER rejects z=0.5 against clear depth=1.
    state.alpha_test=false;state.blend=false;state.depth_test=true;state.depth_func=GL_GREATER_;
    renderer.clear(state);
    renderer.draw(triangle(1,0,0,1,0),state);
    assert(renderer.color_buffer()[1*4+1]==0xFFFF0000u);
    state.depth_func=GL_LESS_;
    renderer.clear(state);
    renderer.draw(triangle(1,0,0,1,0),state);
    assert(renderer.color_buffer()[1*4+1]==0xFF0000FFu);

    bool threw=false;
    try { core.depth_func(0xDEAD); } catch(const std::invalid_argument&) { threw=true; }
    assert(threw);
    threw=false;
    try { core.alpha_func(0xDEAD,0.5f); } catch(const std::invalid_argument&) { threw=true; }
    assert(threw);
    std::cout<<"gl_pipeline_state_test: PASS\\n";
}
