#include "class_loading_graph.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace opti::runtime;

struct ArchiveStorage {
    std::string path;
    std::vector<std::uint8_t> bytes;
    JarInspection inspection;
};
static ArchiveStorage read_archive(const std::string& path, bool require_main=false) {
    ArchiveStorage a; a.path=path;
    std::ifstream in(path,std::ios::binary);
    assert(in.good());
    a.bytes.assign(std::istreambuf_iterator<char>(in),{});
    a.inspection=inspect_jar(a.bytes.data(),a.bytes.size(),require_main);
    if(!a.inspection.valid) {
        std::cerr<<"INVALID JAR "<<path<<": "<<a.inspection.error<<"\n";
        std::abort();
    }
    return a;
}
static ClasspathArchive view(const ArchiveStorage& a) {
    return {a.path,a.bytes.data(),a.bytes.size(),&a.inspection};
}

int main(int argc,char** argv) {
    assert(argc==7);
    // argv: minecraft.jar, lwjgl.jar, lwjgl_util.jar, jinput.jar, rt.jar, bad.jar
    std::vector<ArchiveStorage> archives;
    archives.reserve(6);
    for(int i=1;i<=5;i++) archives.push_back(read_archive(argv[i],i==1));

    RuntimeClassPath cp;
    cp.bootstrap.push_back(view(archives[4]));
    cp.application.push_back(view(archives[0]));
    cp.application.push_back(view(archives[1]));
    cp.application.push_back(view(archives[2]));
    cp.application.push_back(view(archives[3]));

    ClassRepository repository(cp);
    const auto& main_class=repository.load_class("net.minecraft.client.Minecraft");
    assert(main_class.state==ClassRecordState::Loaded);
    assert(main_class.inspection.major==49);
    assert(main_class.inspection.this_class=="net/minecraft/client/Minecraft");
    assert(!main_class.inspection.referenced_classes.empty());
    const auto& main_again=repository.load_class("net.minecraft.client.Minecraft");
    assert(&main_class==&main_again); // stable cached record

    const auto& string_class=repository.load_class("java.lang.String");
    assert(string_class.state==ClassRecordState::Loaded);
    assert(string_class.source_role==ArchiveRole::Bootstrap);

    auto graph=discover_class_dependencies(repository,"net.minecraft.client.Minecraft",2048);
    assert(!graph.nodes.empty() && graph.nodes.size()<=2048);
    assert(graph.loaded_count>1);
    assert(graph.edge_count>0);
    std::set<std::string> unique_names;
    std::size_t back_edges=0;
    for(std::size_t i=0;i<graph.nodes.size();++i) {
        const auto& node=graph.nodes[i];
        assert(unique_names.insert(node.binary_name).second);
        for(const auto target:node.dependency_nodes) {
            assert(target<graph.nodes.size());
            if(target<=i) ++back_edges;
        }
    }
    assert(back_edges>0); // dependency cycles/references resolve to existing graph nodes
    std::cout<<"GRAPH root="<<graph.root_class
             <<" nodes="<<graph.nodes.size()
             <<" loaded="<<graph.loaded_count
             <<" missing="<<graph.missing_count
             <<" failed="<<graph.failed_count
             <<" edges="<<graph.edge_count
             <<" cache_bytes="<<repository.cached_byte_count()
             <<" truncated="<<(graph.truncated?"yes":"no")<<"\n";

    auto bounded=discover_class_dependencies(repository,"net.minecraft.client.Minecraft",32);
    assert(bounded.nodes.size()<=32);
    assert(bounded.truncated);
    assert(bounded.skipped_due_to_limit>0);

    // A class stored under a false ZIP path must be rejected after parsing.
    auto bad=read_archive(argv[6]);
    RuntimeClassPath bad_cp; bad_cp.application.push_back(view(bad));
    ClassRepository bad_repository(bad_cp);
    const auto& mismatch=bad_repository.load_class("test.Mismatch");
    assert(mismatch.state==ClassRecordState::Failed);
    assert(mismatch.error.find("class identity mismatch")!=std::string::npos);

    std::cout<<"PASS: generic JAR inspection, runtime/app class loading, identity checks, cache, "
                "dependency graph, cycles/dedup and graph bounds\n";
}
