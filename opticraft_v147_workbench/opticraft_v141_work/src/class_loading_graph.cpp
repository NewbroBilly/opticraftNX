#include "class_loading_graph.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace opti::runtime {
namespace {
std::string binary_from_internal(const std::string& name) {
    std::string out=name;
    std::replace(out.begin(),out.end(),'/','.');
    return out;
}
const ClasspathArchive* archive_for(const RuntimeClassPath& path,
                                    ArchiveRole role,std::size_t index) {
    const std::vector<ClasspathArchive>* list=nullptr;
    switch(role) {
        case ArchiveRole::Bootstrap: list=&path.bootstrap; break;
        case ArchiveRole::Platform: list=&path.platform; break;
        case ArchiveRole::Application: list=&path.application; break;
    }
    return list && index<list->size()?&(*list)[index]:nullptr;
}
}

ClassRepository::ClassRepository(const RuntimeClassPath& classpath)
    : classpath_(classpath) {}

const ClassRecord& ClassRepository::load_class(const std::string& binary_name) {
    const auto existing=records_.find(binary_name);
    if(existing!=records_.end()) return existing->second;

    ClassRecord record;
    record.binary_name=binary_name;
    auto loaded=load_class_bytes(binary_name,classpath_);
    if(!loaded.found) {
        record.state=loaded.error.find("class not found in classpath:")==0
            ?ClassRecordState::Missing:ClassRecordState::Failed;
        record.error=loaded.error;
        return records_.emplace(binary_name,std::move(record)).first->second;
    }

    record.bytes=std::move(loaded.bytes);
    record.source_role=loaded.source_role;
    record.source_index=loaded.role_index;
    if(const auto* source=archive_for(classpath_,record.source_role,record.source_index))
        record.source_path=source->path;
    record.inspection=inspect_classfile(record.bytes.data(),record.bytes.size());
    if(!record.inspection.valid) {
        record.state=ClassRecordState::Failed;
        record.error="invalid classfile: "+record.inspection.error;
    } else {
        std::string expected=binary_name;
        std::replace(expected.begin(),expected.end(),'.','/');
        if(record.inspection.this_class!=expected) {
            record.state=ClassRecordState::Failed;
            record.error="class identity mismatch: requested "+expected+
                         ", archive contains "+record.inspection.this_class;
        } else {
            record.state=ClassRecordState::Loaded;
        }
    }
    return records_.emplace(binary_name,std::move(record)).first->second;
}

std::size_t ClassRepository::cached_classes() const noexcept {
    return records_.size();
}

std::size_t ClassRepository::cached_byte_count() const noexcept {
    std::size_t total=0;
    for(const auto& item:records_) total+=item.second.bytes.size();
    return total;
}

ClassDependencyGraph discover_class_dependencies(ClassRepository& repository,
                                                 const std::string& root_binary_name,
                                                 std::size_t max_nodes) {
    ClassDependencyGraph graph;
    graph.root_class=root_binary_name;
    if(max_nodes==0) { graph.truncated=true; graph.skipped_due_to_limit=1; return graph; }

    std::map<std::string,std::size_t> indices;
    std::set<std::string> skipped_names;
    std::deque<std::size_t> pending;
    auto add_node=[&](const std::string& name)->std::size_t {
        const auto found=indices.find(name);
        if(found!=indices.end()) return found->second;
        const std::size_t index=graph.nodes.size();
        ClassGraphNode node;
        node.binary_name=name;
        graph.nodes.push_back(std::move(node));
        indices.emplace(name,index);
        pending.push_back(index);
        return index;
    };
    add_node(root_binary_name);

    while(!pending.empty()) {
        const std::size_t index=pending.front();
        pending.pop_front();
        const std::string name=graph.nodes[index].binary_name;
        const ClassRecord& record=repository.load_class(name);
        auto& node=graph.nodes[index];
        node.state=record.state;
        node.source_role=record.source_role;
        node.source_path=record.source_path;
        node.error=record.error;
        node.byte_size=record.bytes.size();
        node.class_major=record.inspection.major;
        node.super_class=record.inspection.super_class;
        node.referenced_class_count=record.inspection.referenced_classes.size();

        if(record.state!=ClassRecordState::Loaded) continue;
        for(const auto& internal:record.inspection.referenced_classes) {
            const std::string dependency=binary_from_internal(internal);
            auto found=indices.find(dependency);
            std::size_t target=0;
            if(found==indices.end()) {
                if(graph.nodes.size()>=max_nodes) {
                    graph.truncated=true;
                    skipped_names.insert(dependency);
                    continue;
                }
                target=add_node(dependency);
            } else {
                target=found->second;
            }
            // node is reacquired after vector growth; references may be invalidated.
            auto& source=graph.nodes[index];
            if(std::find(source.dependency_nodes.begin(),source.dependency_nodes.end(),target)
               ==source.dependency_nodes.end()) {
                source.dependency_nodes.push_back(target);
                ++graph.edge_count;
            }
        }
    }

    graph.skipped_due_to_limit=skipped_names.size();
    for(const auto& node:graph.nodes) {
        switch(node.state) {
            case ClassRecordState::Loaded: ++graph.loaded_count; break;
            case ClassRecordState::Missing: ++graph.missing_count; break;
            case ClassRecordState::Failed: ++graph.failed_count; break;
        }
    }
    return graph;
}

} // namespace opti::runtime
