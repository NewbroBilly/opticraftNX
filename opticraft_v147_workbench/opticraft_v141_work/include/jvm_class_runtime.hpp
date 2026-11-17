#pragma once
#include "class_code_parser.hpp"
#include "jvm_runtime.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace opti::runtime {
enum class InvokeKind { Static, Special, Virtual };
enum class ClassInitializationState { Uninitialized, Initializing, Initialized, Erroneous };
struct ClassInitializationRecord { ClassInitializationState state=ClassInitializationState::Uninitialized; std::uint64_t owner_thread=0; std::string failure; };
struct ResolvedMethod { const ParsedClassCode* owner=nullptr; const MethodCode* method=nullptr; std::string owner_name; };
struct ClassRuntimeResult { bool success=false; std::string error; };
class JvmClassRuntime {
public:
 ClassRuntimeResult register_class(ParsedClassCode cls);
 const ParsedClassCode* find_class(const std::string& name) const;
 ResolvedMethod resolve_method(const std::string& symbolic_owner,const std::string& name,const std::string& descriptor,InvokeKind kind,const std::string& receiver_class={}) const;
 bool is_assignable(const std::string& actual,const std::string& expected) const;
 bool instance_of(const JvmHeap& heap,ObjectRef ref,const std::string& expected) const;
 bool build_instance_layout(const std::string& class_name,std::vector<FieldLayout>& fields,std::string& error) const;
 bool register_resource(const std::string& name, std::vector<std::uint8_t> bytes);
 bool find_resource(const std::string& name, std::vector<std::uint8_t>& bytes) const;
 bool begin_class_initialization(const std::string& name, std::uint64_t thread_id, std::string& error);
 bool complete_class_initialization(const std::string& name, std::uint64_t thread_id);
 bool fail_class_initialization(const std::string& name, std::uint64_t thread_id, const std::string& cause);
 ClassInitializationRecord class_initialization(const std::string& name) const;
 std::size_t class_count() const { return classes_.size(); }
 std::size_t resource_count() const { return resources_.size(); }
private:
 std::unordered_map<std::string,std::shared_ptr<ParsedClassCode>> classes_;
 std::unordered_map<std::string,std::vector<std::uint8_t>> resources_;
 std::unordered_map<std::string,ClassInitializationRecord> initialization_;
 const MethodCode* declared_method(const ParsedClassCode&,const std::string&,const std::string&) const;
};
}
