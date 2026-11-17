#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
namespace opti::runtime {
using ObjectRef=std::uint32_t;
struct NullValue {};
using JvmValue=std::variant<NullValue,std::int32_t,float,std::int64_t,double,ObjectRef>;
enum class HeapKind { Object, IntArray, PrimitiveArray, ReferenceArray };
struct HeapObject {
 HeapKind kind=HeapKind::Object;
 std::string class_name;
 std::string component_descriptor;
 std::string string_value;
 std::unordered_map<std::string,JvmValue> fields;
 std::vector<JvmValue> elements;
 std::vector<std::uint8_t> stream_bytes;
 std::size_t stream_position=0;
};
struct FieldLayout { std::string name,descriptor; bool is_static=false; };
struct HeapStats { std::size_t live_objects=0, allocated_objects=0, collected_objects=0, live_bytes_estimate=0; };
class JvmHeap {
public:
 explicit JvmHeap(std::size_t max_objects=100000):max_objects_(max_objects){}
 ObjectRef allocate_object(const std::string& class_name);
 ObjectRef allocate_string(const std::string& value);
 bool get_string(ObjectRef ref,std::string& value) const;
 ObjectRef allocate_instance(const std::string& class_name,const std::vector<FieldLayout>& fields);
 ObjectRef allocate_int_array(std::size_t length);
 ObjectRef allocate_primitive_array(std::size_t length, char descriptor);
 ObjectRef allocate_reference_array(std::size_t length,const std::string& component_descriptor);
 ObjectRef allocate_byte_stream(const std::vector<std::uint8_t>& bytes);
 bool valid_ref(ObjectRef ref) const;
 const HeapObject* get(ObjectRef ref) const;
 HeapObject* get(ObjectRef ref);
 bool put_field(ObjectRef ref,const std::string& field_key,const JvmValue& value);
 bool get_field(ObjectRef ref,const std::string& field_key,JvmValue& value) const;
 bool array_store(ObjectRef ref,std::size_t index,const JvmValue& value);
 bool array_load(ObjectRef ref,std::size_t index,JvmValue& value) const;
 std::size_t collect_garbage(const std::vector<JvmValue>& roots);
 HeapStats stats() const;
private:
 std::unordered_map<ObjectRef,HeapObject> objects_;
 ObjectRef next_ref_=1;
 std::size_t max_objects_,allocated_=0,collected_=0;
};
class JvmStaticFields {
public:
 bool define(const std::string& class_name,const std::string& field_name,const JvmValue& initial);
 bool set(const std::string& class_name,const std::string& field_name,const JvmValue& value);
 bool get(const std::string& class_name,const std::string& field_name,JvmValue& value) const;
 std::vector<JvmValue> roots() const;
private:
 std::unordered_map<std::string,JvmValue> values_;
 static std::string key(const std::string&,const std::string&);
};
}
