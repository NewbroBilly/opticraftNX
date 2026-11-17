#include "framebuffer_blit.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
namespace opti {
bool blit_rgba8(std::uint8_t* dst,std::size_t dst_stride,int dst_w,int dst_h,
                const std::uint32_t* src,int src_w,int src_h) {
    if(!dst||!src||dst_w<=0||dst_h<=0||src_w<=0||src_h<=0)return false;
    const auto w=static_cast<std::size_t>(dst_w);
    if(w>std::numeric_limits<std::size_t>::max()/4)return false;
    const std::size_t row_bytes=w*4;
    if(dst_stride<row_bytes)return false;
    if(dst_h>1 && dst_stride>std::numeric_limits<std::size_t>::max()/static_cast<std::size_t>(dst_h-1))return false;
    if(src_h>1 && static_cast<std::size_t>(src_w)>std::numeric_limits<std::size_t>::max()/static_cast<std::size_t>(src_h-1))return false;
    // RGBA8 opaque black; byte writes avoid any alignment requirement on dst.
    for(int y=0;y<dst_h;y++) {
        auto* row=dst+static_cast<std::size_t>(y)*dst_stride;
        for(int x=0;x<dst_w;x++) {
            row[static_cast<std::size_t>(x)*4+0]=0;
            row[static_cast<std::size_t>(x)*4+1]=0;
            row[static_cast<std::size_t>(x)*4+2]=0;
            row[static_cast<std::size_t>(x)*4+3]=255;
        }
    }
    const int copy_w=std::min(dst_w,src_w),copy_h=std::min(dst_h,src_h);
    const std::size_t copy_bytes=static_cast<std::size_t>(copy_w)*4;
    for(int y=0;y<copy_h;y++) {
        std::memcpy(dst+static_cast<std::size_t>(y)*dst_stride,
                    src+static_cast<std::size_t>(y)*src_w,copy_bytes);
    }
    return true;
}
}
