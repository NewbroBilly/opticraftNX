#include "classpath_resolver.hpp"
#include <algorithm>

namespace opti::runtime {
namespace {
bool valid_name(const std::string& name) {
    if(name.empty()) return false;
    if(name.front()=='/' || name.back()=='.') return false;
    if(name.find("..")!=std::string::npos) return false;
    for(unsigned char c:name) {
        if(c=='.' || c=='/' || c=='_' || c=='$' ||
           (c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
           (c>='0'&&c<='9')) continue;
        return false;
    }
    return true;
}
}
ResolvedClass resolve_class(const std::string& binary_name,
                            const std::vector<ClasspathArchive>& archives) {
    ResolvedClass out;
    if(!valid_name(binary_name)) {
        out.error="invalid binary class name";
        return out;
    }
    out.internal_name=binary_name;
    std::replace(out.internal_name.begin(),out.internal_name.end(),'.','/');
    const std::string entry_name=out.internal_name+".class";
    for(std::size_t i=0;i<archives.size();++i) {
        const auto& archive=archives[i];
        if(!archive.bytes || !archive.inspection || !archive.inspection->valid) {
            out.error="classpath contains an unavailable or invalid archive";
            return out;
        }
        const JarEntry* entry=find_jar_entry(*archive.inspection,entry_name);
        if(entry) {
            out.found=true;
            out.archive_index=i;
            out.entry=*entry;
            return out;
        }
    }
    out.error="class not found in classpath: "+entry_name;
    return out;
}
bool read_resolved_class(const ResolvedClass& resolved,
                         const std::vector<ClasspathArchive>& archives,
                         std::vector<std::uint8_t>& class_bytes,
                         std::string* error) {
    auto fail=[&](const std::string& why) {
        if(error) *error=why;
        class_bytes.clear();
        return false;
    };
    if(!resolved.found) return fail("cannot read unresolved class");
    if(resolved.archive_index>=archives.size()) return fail("archive index out of range");
    const auto& archive=archives[resolved.archive_index];
    if(!archive.bytes || !archive.inspection || !archive.inspection->valid)
        return fail("resolved archive is no longer available");
    const JarEntry* verified=find_jar_entry(*archive.inspection,resolved.entry.name);
    if(!verified || verified->crc32!=resolved.entry.crc32 ||
       verified->local_header_offset!=resolved.entry.local_header_offset)
        return fail("resolved entry no longer matches inspected archive");
    return extract_jar_entry(archive.bytes,archive.size,*verified,class_bytes,error);
}
}
