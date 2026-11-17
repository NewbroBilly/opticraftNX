#include "classfile_inspector.hpp"
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace opti::runtime {
namespace {
struct Reader {
    const std::uint8_t* p; std::size_t n, pos=0;
    bool take(std::size_t count) {
        if(count>n-pos) return false;
        pos+=count; return true;
    }
    bool u1(std::uint8_t& v) {
        if(!take(1)) return false;
        v=p[pos-1]; return true;
    }
    bool u2(std::uint16_t& v) {
        if(!take(2)) return false;
        v=(std::uint16_t(p[pos-2])<<8)|p[pos-1]; return true;
    }
    bool u4(std::uint32_t& v) {
        if(!take(4)) return false;
        v=(std::uint32_t(p[pos-4])<<24)|(std::uint32_t(p[pos-3])<<16)|
          (std::uint32_t(p[pos-2])<<8)|p[pos-1]; return true;
    }
};
struct CP { std::uint8_t tag=0; std::uint16_t a=0,b=0; std::string utf8; };
bool check_index(const std::vector<CP>& cp,std::uint16_t i,std::uint8_t tag) {
    return i>0 && i<cp.size() && cp[i].tag==tag;
}
bool valid_internal_name(const std::string& name) {
    if(name.empty() || name.front()=='/' || name.back()=='/' ||
       name.find("..")!=std::string::npos || name.find('.')!=std::string::npos)
        return false;
    for(unsigned char c:name) {
        if(c=='/' || c=='$' || c=='_' || c=='-' ||
           (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9')) continue;
        return false;
    }
    return true;
}
void scan_descriptor_classes(const std::string& text,std::set<std::string>& found) {
    // Parse each object type from descriptors/signatures. Advance over the
    // complete token so uppercase 'L' inside a name (e.g. LayoutManager)
    // cannot be mistaken for another CONSTANT_Class marker. Generic arguments
    // are scanned recursively by continuing immediately after '<'.
    std::size_t pos=0;
    while((pos=text.find('L',pos))!=std::string::npos) {
        const std::size_t start=pos+1;
        const std::size_t semicolon=text.find(';',start);
        const std::size_t generic=text.find('<',start);
        if(semicolon==std::string::npos && generic==std::string::npos) break;
        const bool has_generic=generic!=std::string::npos &&
                               (semicolon==std::string::npos || generic<semicolon);
        const std::size_t boundary=has_generic?generic:semicolon;
        std::string name=text.substr(start,boundary-start);
        if(valid_internal_name(name)) found.insert(name);
        pos=has_generic?generic+1:semicolon+1;
    }
}

bool skip_attributes(Reader& r,const std::vector<CP>& cp,std::uint16_t count) {
    for(std::uint16_t i=0;i<count;i++) {
        std::uint16_t name; std::uint32_t len;
        if(!r.u2(name)||!r.u4(len)||!check_index(cp,name,1)||!r.take(len)) return false;
    }
    return true;
}
bool skip_members(Reader& r,const std::vector<CP>& cp,std::uint16_t count,
                  std::set<std::string>& referenced) {
    for(std::uint16_t i=0;i<count;i++) {
        std::uint16_t flags,name,desc,attrs;
        if(!r.u2(flags)||!r.u2(name)||!r.u2(desc)||!r.u2(attrs)||
           !check_index(cp,name,1)||!check_index(cp,desc,1)||
           !skip_attributes(r,cp,attrs)) return false;
        scan_descriptor_classes(cp[desc].utf8,referenced);
    }
    return true;
}
}
ClassInspection inspect_classfile(const std::uint8_t* data,std::size_t size) {
    ClassInspection out; out.byte_size=size;
    auto fail=[&](const char* why) { out.error=why; return out; };
    if(!data||size<10) return fail("classfile header truncated");
    Reader r{data,size};
    std::uint32_t magic;
    if(!r.u4(magic)||magic!=0xcafebabe) return fail("invalid CAFEBABE magic");
    if(!r.u2(out.minor)||!r.u2(out.major)||!r.u2(out.constant_pool_count))
        return fail("classfile header truncated");
    if(out.major<45||out.major>65) return fail("unsupported classfile major version");
    if(out.constant_pool_count==0) return fail("zero constant pool");
    std::vector<CP> cp(out.constant_pool_count);
    for(std::uint32_t i=1;i<out.constant_pool_count;i++) {
        std::uint8_t tag;
        if(!r.u1(tag)) return fail("truncated constant pool tag");
        cp[i].tag=tag;
        std::uint16_t a;
        switch(tag) {
        case 1:
            if(!r.u2(a)||!r.take(a)) return fail("truncated UTF8 constant");
            cp[i].utf8.assign(reinterpret_cast<const char*>(data+r.pos-a),a);
            break;
        case 3: case 4:
            if(!r.take(4)) return fail("truncated numeric constant");
            break;
        case 5: case 6:
            if(!r.take(8)||i+1>=out.constant_pool_count) return fail("truncated wide constant");
            cp[++i].tag=0; break;
        case 7: case 8: case 16: case 19: case 20:
            if(!r.u2(cp[i].a)) return fail("truncated indexed constant");
            break;
        case 9: case 10: case 11: case 12: case 17: case 18:
            if(!r.u2(cp[i].a)||!r.u2(cp[i].b)) return fail("truncated paired constant");
            break;
        case 15: {
            std::uint8_t kind;
            if(!r.u1(kind)||!r.u2(cp[i].a)||kind<1||kind>9)
                return fail("invalid method handle");
            cp[i].b=kind;
            break;
        }
        default: return fail("unsupported constant pool tag");
        }
    }
    std::set<std::string> referenced;
    for(std::size_t i=1;i<cp.size();i++) {
        const auto& e=cp[i];
        switch(e.tag) {
        case 7: case 8: case 16: case 19: case 20:
            if(!check_index(cp,e.a,1)) return fail("bad constant UTF8 reference");
            if(e.tag==16) scan_descriptor_classes(cp[e.a].utf8,referenced);
            if(e.tag==7) {
                const auto& name=cp[e.a].utf8;
                if(!name.empty() && name.front()=='[') scan_descriptor_classes(name,referenced);
                else if(valid_internal_name(name)) referenced.insert(name);
            }
            break;
        case 9: case 10: case 11:
            if(!check_index(cp,e.a,7)||!check_index(cp,e.b,12))
                return fail("bad member constant reference");
            break;
        case 12:
            if(!check_index(cp,e.a,1)||!check_index(cp,e.b,1))
                return fail("bad name-and-type reference");
            scan_descriptor_classes(cp[e.b].utf8,referenced);
            break;
        case 17: case 18:
            if(!check_index(cp,e.b,12)) return fail("bad dynamic name-and-type reference");
            break;
        case 15: {
            const auto tag= e.a<cp.size()?cp[e.a].tag:0;
            if(tag!=9 && tag!=10 && tag!=11) return fail("bad method handle target");
            break;
        }
        default: break;
        }
    }
    std::uint16_t flags,self,parent,interfaces;
    if(!r.u2(flags)||!r.u2(self)||!r.u2(parent)||!r.u2(interfaces))
        return fail("truncated class declaration");
    if(!check_index(cp,self,7)|| (parent!=0&&!check_index(cp,parent,7)))
        return fail("invalid this/super class reference");
    out.this_class=cp[cp[self].a].utf8;
    if(parent) out.super_class=cp[cp[parent].a].utf8;
    if(!out.super_class.empty()) referenced.insert(out.super_class);
    referenced.erase(out.this_class);
    out.referenced_classes.assign(referenced.begin(),referenced.end());
    for(std::uint16_t i=0;i<interfaces;i++) {
        std::uint16_t idx;
        if(!r.u2(idx)||!check_index(cp,idx,7)) return fail("invalid interface reference");
    }
    if(!r.u2(out.fields_count)||!skip_members(r,cp,out.fields_count,referenced))
        return fail("invalid fields");
    if(!r.u2(out.methods_count)||!skip_members(r,cp,out.methods_count,referenced))
        return fail("invalid methods");
    if(!r.u2(out.attributes_count)||!skip_attributes(r,cp,out.attributes_count))
        return fail("invalid class attributes");
    if(r.pos!=size) return fail("trailing classfile bytes");
    out.valid=true; return out;
}
}
