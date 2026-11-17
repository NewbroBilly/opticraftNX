#include "java_runtime_bootstrap.hpp"
#include <algorithm>
#include <limits>
#include <zlib.h>

namespace opti::runtime {
namespace {
std::uint16_t u16(const std::uint8_t* p){return std::uint16_t(p[0])|(std::uint16_t(p[1])<<8);}
std::uint32_t u32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
bool range_ok(std::size_t offset,std::size_t length,std::size_t total){return offset<=total&&length<=total-offset;}
std::string join(const std::vector<std::string>& v,char sep) {
    std::string out;
    for(const auto& s:v){if(!out.empty())out+=sep;out+=s;}
    return out;
}
}
JarInspection inspect_jar(const std::uint8_t* b,std::size_t n,bool require_minecraft_main) {
    JarInspection out;
    if(!b||n<22){out.error="archive is null or too small";return out;}
    const std::size_t search_start=n>65557?n-65557:0;
    std::size_t eocd=n;
    for(std::size_t p=n-22;;--p) {
        if(u32(b+p)==0x06054b50u && range_ok(p,22u+u16(b+p+20),n) &&
           p+22u+u16(b+p+20)==n){eocd=p;break;}
        if(p==search_start)break;
    }
    if(eocd==n){out.error="ZIP end-of-central-directory not found";return out;}
    const auto disk=u16(b+eocd+4),cd_disk=u16(b+eocd+6);
    const auto entries_disk=u16(b+eocd+8),entries_total=u16(b+eocd+10);
    const auto cd_size=u32(b+eocd+12),cd_offset=u32(b+eocd+16);
    const auto comment=u16(b+eocd+20);
    if(disk||cd_disk||entries_disk!=entries_total||entries_total==0xFFFF||
       cd_size==0xFFFFFFFFu||cd_offset==0xFFFFFFFFu||
       !range_ok(eocd,22u+comment,n)||!range_ok(cd_offset,cd_size,n)||
       std::size_t(cd_offset)+cd_size>eocd) {
        out.error="unsupported or malformed ZIP directory";return out;
    }
    std::size_t p=cd_offset;
    out.entries.reserve(entries_total);
    for(std::uint32_t i=0;i<entries_total;i++) {
        if(!range_ok(p,46,n)||u32(b+p)!=0x02014b50u){out.error="invalid central directory entry";return out;}
        JarEntry e;
        e.flags=u16(b+p+8);e.compression=u16(b+p+10);e.crc32=u32(b+p+16);
        e.compressed_size=u32(b+p+20);e.uncompressed_size=u32(b+p+24);
        const auto name_len=u16(b+p+28),extra_len=u16(b+p+30),comment_len=u16(b+p+32);
        e.local_header_offset=u32(b+p+42);
        const std::size_t full=46u+name_len+extra_len+comment_len;
        if(!range_ok(p,full,n)||!range_ok(e.local_header_offset,30,n)||
           u32(b+e.local_header_offset)!=0x04034b50u) {
            out.error="truncated entry or invalid local header";return out;
        }
        e.name.assign(reinterpret_cast<const char*>(b+p+46),name_len);
        if(e.name.empty()||e.name.find('\0')!=std::string::npos){out.error="invalid ZIP entry name";return out;}
        if(e.flags&1u){out.error="encrypted JAR entries are unsupported";return out;}
        if(e.compression!=0&&e.compression!=8){out.error="unsupported ZIP compression method";return out;}
        if(e.name.size()>=6&&e.name.compare(e.name.size()-6,6,".class")==0)++out.class_count;
        else if(!e.name.empty()&&e.name.back()!='/')++out.resource_count;
        if(e.name=="net/minecraft/client/Minecraft.class")out.has_minecraft_main=true;
        out.entries.push_back(std::move(e));p+=full;
    }
    if(p!=std::size_t(cd_offset)+cd_size){out.error="central directory size mismatch";return out;}
    if(require_minecraft_main && !out.has_minecraft_main){out.error="Minecraft main class is missing";return out;}
    out.valid=true;return out;
}
bool extract_jar_entry(const std::uint8_t* b,std::size_t n,const JarEntry& e,
                       std::vector<std::uint8_t>& output,std::string* error) {
    auto fail=[&](const char* message){if(error)*error=message;output.clear();return false;};
    output.clear();
    if(!b)return fail("archive data is null");
    if(e.compression!=0&&e.compression!=8)return fail("unsupported compression method");
    if(!range_ok(e.local_header_offset,30,n))return fail("local header is out of range");
    const auto* h=b+e.local_header_offset;
    if(u32(h)!=0x04034b50u)return fail("invalid local file header signature");
    const std::size_t name_len=u16(h+26),extra_len=u16(h+28);
    const std::size_t data_offset=std::size_t(e.local_header_offset)+30u+name_len+extra_len;
    if(!range_ok(data_offset,e.compressed_size,n))return fail("compressed data is out of range");
    output.resize(e.uncompressed_size);
    const auto* src=b+data_offset;
    if(e.compression==0) {
        if(e.compressed_size!=e.uncompressed_size)return fail("stored entry size mismatch");
        if(e.uncompressed_size)std::copy(src,src+e.compressed_size,output.begin());
    } else {
        z_stream stream{};
        stream.next_in=const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
        stream.avail_in=e.compressed_size;
        static std::uint8_t empty_output=0;
        stream.next_out=output.empty()?&empty_output:reinterpret_cast<Bytef*>(output.data());
        stream.avail_out=output.empty()?1u:static_cast<uInt>(output.size());
        if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK)return fail("inflate initialization failed");
        const int status=inflate(&stream,Z_FINISH);
        const bool ok=status==Z_STREAM_END && stream.total_out==e.uncompressed_size &&
                      stream.total_in==e.compressed_size;
        inflateEnd(&stream);
        if(!ok)return fail("DEFLATE stream or uncompressed size mismatch");
    }
    const auto actual=static_cast<std::uint32_t>(::crc32(0L,
        output.empty()?Z_NULL:reinterpret_cast<const Bytef*>(output.data()),
        static_cast<uInt>(output.size())));
    if(actual!=e.crc32)return fail("CRC32 mismatch");
    if(error)error->clear();
    return true;
}

const JarEntry* find_jar_entry(const JarInspection& inspection,const std::string& name) {
    for(const auto& entry:inspection.entries)if(entry.name==name)return &entry;
    return nullptr;
}

LaunchPlan make_launch_plan(const std::string& root,const std::vector<std::string>& args) {
    LaunchPlan p;p.data_root=root;
    if(root.empty()){p.error="data root is empty";return p;}
    if(root.back()=='/'||root.back()=='\\'){p.error="data root must not end with a separator";return p;}
    if(root.find(';')!=std::string::npos){p.error="data root contains classpath separator";return p;}
    p.classpath={root+"/minecraft.jar",root+"/lwjgl.jar",root+"/lwjgl_util.jar",
                 root+"/jinput.jar",root+"/switchport.jar"};
    p.joined_classpath=join(p.classpath,';');
    p.arguments=args;
    p.valid=true;return p;
}
BootstrapResult bootstrap(const LaunchPlan& p,const RuntimeCallbacks& cb) {
    if(!p.valid)return {false,"plan",p.error.empty()?"launch plan is invalid":p.error};
    if(!cb.file_exists||!cb.load_vm||!cb.invoke_main)
        return {false,"callbacks","required runtime callback is missing"};
    for(const auto& file:p.classpath) {
        if(!cb.file_exists(cb.user,file.c_str()))
            return {false,"classpath","required classpath file missing: "+file};
    }
    if(!cb.load_vm(cb.user,p.joined_classpath.c_str(),p.main_class.c_str()))
        return {false,"load_vm","VM loader callback failed"};
    std::vector<const char*> argv;argv.reserve(p.arguments.size());
    for(const auto& arg:p.arguments)argv.push_back(arg.c_str());
    if(!cb.invoke_main(cb.user,p.main_class.c_str(),static_cast<int>(argv.size()),argv.data()))
        return {false,"invoke_main","Java main invocation callback failed"};
    return {true,"","runtime bootstrap callbacks completed"};
}
}
