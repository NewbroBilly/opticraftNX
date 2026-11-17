#include "class_loading_graph.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace opti::runtime;
struct ArchiveStorage {
    std::string path;
    std::vector<std::uint8_t> bytes;
    JarInspection inspection;
};
struct Options {
    std::string root;
    std::size_t max_nodes=2048;
    std::string nodes_csv="class_nodes.csv";
    std::string edges_csv="class_edges.csv";
    std::vector<std::string> bootstrap, platform, application;
};
static bool parse_size(const std::string& value,std::size_t& out) {
    if(value.empty()) return false;
    char* end=nullptr;
    const auto n=std::strtoull(value.c_str(),&end,10);
    if(!end || *end!='\0' || n==0 || n>1000000) return false;
    out=static_cast<std::size_t>(n); return true;
}
static bool parse_args(int argc,char** argv,Options& o) {
    if(argc<4) return false;
    o.root=argv[1];
    if(!parse_size(argv[2],o.max_nodes)) return false;
    for(int i=3;i<argc;i++) {
        const std::string arg=argv[i];
        if(arg.rfind("--bootstrap=",0)==0) o.bootstrap.push_back(arg.substr(12));
        else if(arg.rfind("--platform=",0)==0) o.platform.push_back(arg.substr(11));
        else if(arg.rfind("--app=",0)==0) o.application.push_back(arg.substr(6));
        else if(arg.rfind("--nodes=",0)==0) o.nodes_csv=arg.substr(8);
        else if(arg.rfind("--edges=",0)==0) o.edges_csv=arg.substr(8);
        else return false;
    }
    return !o.bootstrap.empty() || !o.platform.empty() || !o.application.empty();
}
static std::string csv(const std::string& value) {
    std::string out="\"";
    for(char c:value) { if(c=='\"') out+="\"\""; else out+=c; }
    return out+"\"";
}
static const char* state_name(ClassRecordState s) {
    switch(s) {
        case ClassRecordState::Loaded:return "loaded";
        case ClassRecordState::Missing:return "missing";
        case ClassRecordState::Failed:return "failed";
    }
    return "unknown";
}
static bool write_reports(const ClassDependencyGraph& graph,const Options& options) {
    std::ofstream nodes(options.nodes_csv,std::ios::binary);
    std::ofstream edges(options.edges_csv,std::ios::binary);
    if(!nodes || !edges) return false;
    nodes<<"class,state,source,major,bytes,super_class,reference_count,error\n";
    edges<<"from_class,to_class\n";
    for(const auto& n:graph.nodes) {
        nodes<<csv(n.binary_name)<<','<<state_name(n.state)<<','<<csv(n.source_path)<<','
             <<n.class_major<<','<<n.byte_size<<','<<csv(n.super_class)<<','
             <<n.referenced_class_count<<','<<csv(n.error)<<'\n';
        for(const auto target:n.dependency_nodes)
            if(target<graph.nodes.size())
                edges<<csv(n.binary_name)<<','<<csv(graph.nodes[target].binary_name)<<'\n';
    }
    return nodes.good()&&edges.good();
}
int main(int argc,char** argv) {
    Options options;
    if(!parse_args(argc,argv,options)) {
        std::cerr<<"Usage: class_dependency_scan ROOT_CLASS MAX_NODES "
                    "[--bootstrap=rt.jar] [--platform=ext.jar] [--app=minecraft.jar] "
                    "[--nodes=nodes.csv] [--edges=edges.csv]\n";
        return 2;
    }
    const std::size_t count=options.bootstrap.size()+options.platform.size()+options.application.size();
    std::vector<ArchiveStorage> storage; storage.reserve(count);
    RuntimeClassPath classpath;
    auto add=[&](const std::vector<std::string>& paths,ArchiveRole role)->bool {
        for(const auto& path:paths) {
            ArchiveStorage item; item.path=path;
            std::ifstream in(path,std::ios::binary);
            if(!in) { std::cerr<<"Cannot open: "<<path<<'\n'; return false; }
            item.bytes.assign(std::istreambuf_iterator<char>(in),{});
            if(item.bytes.empty()) { std::cerr<<"Empty archive: "<<path<<'\n'; return false; }
            item.inspection=inspect_jar(item.bytes.data(),item.bytes.size(),false);
            if(!item.inspection.valid) {
                std::cerr<<"Invalid JAR "<<path<<": "<<item.inspection.error<<'\n';
                return false;
            }
            storage.push_back(std::move(item));
            const auto& a=storage.back();
            ClasspathArchive view{a.path,a.bytes.data(),a.bytes.size(),&a.inspection};
            switch(role) {
                case ArchiveRole::Bootstrap: classpath.bootstrap.push_back(view); break;
                case ArchiveRole::Platform: classpath.platform.push_back(view); break;
                case ArchiveRole::Application: classpath.application.push_back(view); break;
            }
            std::cout<<"JAR "<<path<<" entries="<<a.inspection.entries.size()
                     <<" classes="<<a.inspection.class_count<<'\n';
        }
        return true;
    };
    if(!add(options.bootstrap,ArchiveRole::Bootstrap) ||
       !add(options.platform,ArchiveRole::Platform) ||
       !add(options.application,ArchiveRole::Application)) return 3;

    ClassRepository repository(classpath);
    const auto graph=discover_class_dependencies(repository,options.root,options.max_nodes);
    if(graph.nodes.empty() || graph.nodes.front().state!=ClassRecordState::Loaded) {
        std::cerr<<"Root class could not be loaded: "
                 <<(graph.nodes.empty()?"no graph":graph.nodes.front().error)<<'\n';
        return 4;
    }
    if(!write_reports(graph,options)) {
        std::cerr<<"Cannot write CSV reports\n"; return 5;
    }
    std::cout<<"ROOT "<<graph.root_class<<"\n"
             <<"NODES "<<graph.nodes.size()<<" LOADED "<<graph.loaded_count
             <<" MISSING "<<graph.missing_count<<" FAILED "<<graph.failed_count<<"\n"
             <<"EDGES "<<graph.edge_count<<" CACHE_BYTES "<<repository.cached_byte_count()<<"\n"
             <<"TRUNCATED "<<(graph.truncated?"yes":"no")
             <<" SKIPPED "<<graph.skipped_due_to_limit<<"\n"
             <<"REPORTS "<<options.nodes_csv<<" ; "<<options.edges_csv<<"\n";
    return graph.failed_count?6:0;
}
