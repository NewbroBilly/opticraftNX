#include "runtime_classpath.hpp"

namespace opti::runtime {
std::vector<ClasspathArchive> parent_first_order(const RuntimeClassPath& path) {
    std::vector<ClasspathArchive> ordered;
    ordered.reserve(path.bootstrap.size()+path.platform.size()+path.application.size());
    ordered.insert(ordered.end(),path.bootstrap.begin(),path.bootstrap.end());
    ordered.insert(ordered.end(),path.platform.begin(),path.platform.end());
    ordered.insert(ordered.end(),path.application.begin(),path.application.end());
    return ordered;
}

bool split_classpath(const std::string& value, char separator,
                     std::vector<std::string>& paths, std::string* error) {
    paths.clear();
    auto fail=[&](const char* message) {
        paths.clear();
        if(error) *error=message;
        return false;
    };
    if(separator=='\0') return fail("classpath separator cannot be NUL");
    if(value.empty()) return fail("classpath is empty");
    std::size_t begin=0;
    while(begin<=value.size()) {
        const std::size_t end=value.find(separator,begin);
        const std::size_t stop=end==std::string::npos?value.size():end;
        if(stop==begin) return fail("classpath contains an empty component");
        paths.emplace_back(value.substr(begin,stop-begin));
        if(end==std::string::npos) break;
        begin=end+1;
    }
    return true;
}

LoadedClassBytes load_class_bytes(const std::string& binary_name,
                                  const RuntimeClassPath& path) {
    LoadedClassBytes out;
    const auto ordered=parent_first_order(path);
    out.resolved=resolve_class(binary_name,ordered);
    if(!out.resolved.found) {
        out.error=out.resolved.error;
        return out;
    }
    const std::size_t index=out.resolved.archive_index;
    if(index<path.bootstrap.size()) {
        out.source_role=ArchiveRole::Bootstrap;
        out.role_index=index;
    } else if(index<path.bootstrap.size()+path.platform.size()) {
        out.source_role=ArchiveRole::Platform;
        out.role_index=index-path.bootstrap.size();
    } else {
        out.source_role=ArchiveRole::Application;
        out.role_index=index-path.bootstrap.size()-path.platform.size();
    }
    std::string error;
    if(!read_resolved_class(out.resolved,ordered,out.bytes,&error)) {
        out.error=error;
        out.bytes.clear();
        return out;
    }
    out.found=true;
    return out;
}
}
