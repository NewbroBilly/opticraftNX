#pragma once
#include "runtime_classpath.hpp"
#include "classfile_inspector.hpp"
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace opti::runtime {

enum class ClassRecordState { Loaded, Missing, Failed };

struct ClassRecord {
    std::string binary_name;
    ClassRecordState state=ClassRecordState::Missing;
    ArchiveRole source_role=ArchiveRole::Application;
    std::size_t source_index=0;
    std::string source_path;
    std::vector<std::uint8_t> bytes;
    ClassInspection inspection;
    std::string error;
};

// A host-testable repository/cache over the immutable runtime classpath.
// It reads and structurally inspects classes, but does not define Java objects.
class ClassRepository {
public:
    explicit ClassRepository(const RuntimeClassPath& classpath);
    const ClassRecord& load_class(const std::string& binary_name);
    std::size_t cached_classes() const noexcept;
    std::size_t cached_byte_count() const noexcept;
private:
    const RuntimeClassPath& classpath_;
    std::map<std::string,ClassRecord> records_;
};

struct ClassGraphNode {
    std::string binary_name;
    ClassRecordState state=ClassRecordState::Missing;
    ArchiveRole source_role=ArchiveRole::Application;
    std::string source_path;
    std::string super_class;
    std::size_t referenced_class_count=0;
    std::uint16_t class_major=0;
    std::size_t byte_size=0;
    std::string error;
    std::vector<std::size_t> dependency_nodes;
};

struct ClassDependencyGraph {
    std::string root_class;
    std::vector<ClassGraphNode> nodes;
    std::size_t loaded_count=0;
    std::size_t missing_count=0;
    std::size_t failed_count=0;
    std::size_t edge_count=0;
    std::size_t skipped_due_to_limit=0;
    bool truncated=false;
};

// Discovers the transitive symbolic dependency graph breadth-first. Repeated
// references and cycles are represented as edges to existing nodes, not as
// recursive-load failures. max_nodes bounds memory on constrained devices.
ClassDependencyGraph discover_class_dependencies(ClassRepository& repository,
                                                 const std::string& root_binary_name,
                                                 std::size_t max_nodes=4096);

} // namespace opti::runtime
