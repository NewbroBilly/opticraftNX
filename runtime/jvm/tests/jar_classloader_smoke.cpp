#include "opticraft_method_executor.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <zlib.h>

static void le16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
static void le32(std::vector<uint8_t> &v, uint32_t x) {
    le16(v, static_cast<uint16_t>(x));
    le16(v, static_cast<uint16_t>(x >> 16));
}
static void be16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}
static void be32(std::vector<uint8_t> &v, uint32_t x) {
    be16(v, static_cast<uint16_t>(x >> 16));
    be16(v, static_cast<uint16_t>(x));
}
static void append(std::vector<uint8_t> &v, const uint8_t *p, size_t n) {
    v.insert(v.end(), p, p + n);
}
static void utf8(std::vector<uint8_t> &v, const std::string &text) {
    v.push_back(1);
    be16(v, static_cast<uint16_t>(text.size()));
    append(v, reinterpret_cast<const uint8_t *>(text.data()), text.size());
}
static void append_method(std::vector<uint8_t> &v, uint16_t access,
                          uint16_t name_index, uint16_t descriptor_index,
                          uint16_t max_stack, uint16_t max_locals,
                          const std::vector<uint8_t> &code) {
    be16(v, access);
    be16(v, name_index);
    be16(v, descriptor_index);
    be16(v, 1);
    be16(v, 7); // Code
    be32(v, static_cast<uint32_t>(12 + code.size()));
    be16(v, max_stack);
    be16(v, max_locals);
    be32(v, static_cast<uint32_t>(code.size()));
    append(v, code.data(), code.size());
    be16(v, 0); // exception table
    be16(v, 0); // nested attributes
}
static std::vector<uint8_t> make_main_class() {
    std::vector<uint8_t> v={0xca,0xfe,0xba,0xbe};
    be16(v, 0); // minor version
    be16(v, 49); // Java 5
    be16(v, 20); // constant_pool_count
    utf8(v, "T");                         // #1
    v.push_back(7); be16(v, 1);           // #2 Class T
    utf8(v, "java/lang/Object");          // #3
    v.push_back(7); be16(v, 3);           // #4 Class Object
    utf8(v, "main");                      // #5
    utf8(v, "([Ljava/lang/String;)V");    // #6
    utf8(v, "Code");                      // #7
    utf8(v, "answer");                    // #8
    utf8(v, "()I");                       // #9
    v.push_back(12); be16(v, 8); be16(v, 9); // #10 NameAndType
    v.push_back(10); be16(v, 2); be16(v, 10); // #11 Methodref T.answer
    utf8(v, "combine");                   // #12
    utf8(v, "(II)I");                     // #13
    v.push_back(12); be16(v, 12); be16(v, 13); // #14 NameAndType
    v.push_back(10); be16(v, 2); be16(v, 14); // #15 Methodref T.combine
    utf8(v, "Helper");                    // #16
    v.push_back(7); be16(v, 16);          // #17 Class Helper
    v.push_back(12); be16(v, 8); be16(v, 9); // #18 NameAndType Helper.answer
    v.push_back(10); be16(v, 17); be16(v, 18); // #19 Methodref Helper.answer

    be16(v, 0x0021); // public, super
    be16(v, 2);      // this_class
    be16(v, 4);      // super_class
    be16(v, 0);      // interfaces
    be16(v, 0);      // fields
    be16(v, 3);      // methods
    append_method(v, 0x0009, 5, 6, 2, 1,
                  {0x10,20,0x10,3,0xb8,0x00,0x0f,0x57,
                   0xb8,0x00,0x13,0x57,0xb8,0x00,0x0b,0x57,0xb1});
    append_method(v, 0x0008, 8, 9, 2, 0,
                  {0x10,21,0x05,0x68,0xac});
    append_method(v, 0x0008, 12, 13, 2, 2,
                  {0x1a,0x1b,0x64,0xac});
    be16(v, 0); // class attributes
    return v;
}
static std::vector<uint8_t> make_helper_class() {
    std::vector<uint8_t> v={0xca,0xfe,0xba,0xbe};
    be16(v, 0);
    be16(v, 49);
    be16(v, 8);
    utf8(v, "Helper");              // #1
    v.push_back(7); be16(v, 1);     // #2 Class Helper
    utf8(v, "java/lang/Object");    // #3
    v.push_back(7); be16(v, 3);     // #4 Class Object
    utf8(v, "answer");              // #5
    utf8(v, "()I");                 // #6
    utf8(v, "Code");                // #7
    be16(v, 0x0021);
    be16(v, 2);
    be16(v, 4);
    be16(v, 0);
    be16(v, 0);
    be16(v, 1);
    append_method(v, 0x0009, 5, 6, 2, 0,
                  {0x10,40,0x05,0x60,0xac});
    be16(v, 0);
    return v;
}

struct ZipItem {
    std::string name;
    std::vector<uint8_t> bytes;
    uint32_t crc = 0;
    uint32_t local_offset = 0;
};
static void append_local_entry(std::vector<uint8_t> &jar, ZipItem &item) {
    item.local_offset = static_cast<uint32_t>(jar.size());
    item.crc = static_cast<uint32_t>(crc32(
        0L, reinterpret_cast<const Bytef *>(item.bytes.data()),
        static_cast<uInt>(item.bytes.size())));
    le32(jar, 0x04034b50); le16(jar, 20); le16(jar, 0); le16(jar, 0);
    le16(jar, 0); le16(jar, 0); le32(jar, item.crc);
    le32(jar, static_cast<uint32_t>(item.bytes.size()));
    le32(jar, static_cast<uint32_t>(item.bytes.size()));
    le16(jar, static_cast<uint16_t>(item.name.size())); le16(jar, 0);
    append(jar, reinterpret_cast<const uint8_t *>(item.name.data()),
           item.name.size());
    append(jar, item.bytes.data(), item.bytes.size());
}
static void append_central_directory(std::vector<uint8_t> &jar,
                                     const std::vector<ZipItem> &items) {
    const uint32_t directory_offset = static_cast<uint32_t>(jar.size());
    for (const ZipItem &item : items) {
        le32(jar, 0x02014b50); le16(jar, 20); le16(jar, 20);
        le16(jar, 0); le16(jar, 0); le16(jar, 0); le16(jar, 0);
        le32(jar, item.crc);
        le32(jar, static_cast<uint32_t>(item.bytes.size()));
        le32(jar, static_cast<uint32_t>(item.bytes.size()));
        le16(jar, static_cast<uint16_t>(item.name.size()));
        le16(jar, 0); le16(jar, 0); le16(jar, 0); le16(jar, 0);
        le32(jar, 0); le32(jar, item.local_offset);
        append(jar, reinterpret_cast<const uint8_t *>(item.name.data()),
               item.name.size());
    }
    const uint32_t directory_size =
        static_cast<uint32_t>(jar.size()) - directory_offset;
    le32(jar, 0x06054b50); le16(jar, 0); le16(jar, 0);
    le16(jar, static_cast<uint16_t>(items.size()));
    le16(jar, static_cast<uint16_t>(items.size()));
    le32(jar, directory_size); le32(jar, directory_offset); le16(jar, 0);
}

int main() {
    std::vector<ZipItem> items={
        {"T.class", make_main_class()},
        {"Helper.class", make_helper_class()}
    };
    std::vector<uint8_t> jar;
    for (ZipItem &item : items) append_local_entry(jar, item);
    append_central_directory(jar, items);

    const char *tmpdir=std::getenv("TMPDIR");
    const std::string temporary_directory=
        tmpdir!=nullptr&&tmpdir[0]!='\0'?tmpdir:"/tmp";
    const std::string path = temporary_directory + "/opticraft-loader-smoke-" +
                             std::to_string(static_cast<long long>(getpid())) + ".jar";
    {
        std::ofstream file(path, std::ios::binary);
        assert(file.good());
        file.write(reinterpret_cast<const char *>(jar.data()),
                   static_cast<std::streamsize>(jar.size()));
        assert(file.good());
    }

    OptiJar *archive = nullptr;
    assert(opticraft_jar_open_file(path.c_str(), 1024 * 1024, &archive));
    OptiLoadedClass *loaded = nullptr;
    assert(opticraft_jar_load_class(archive, "T", &loaded));
    const char *class_name = nullptr;
    uint16_t class_name_length = 0;
    assert(opticraft_loaded_class_name(loaded, 0, &class_name, &class_name_length));
    assert(std::string(class_name, class_name_length) == "T");
    const char *super_name = nullptr;
    uint16_t super_name_length = 0;
    assert(opticraft_loaded_class_name(loaded, 1, &super_name, &super_name_length));
    assert(std::string(super_name, super_name_length) == "java/lang/Object");

    OptiMethodReference reference{};
    assert(opticraft_loaded_class_method_ref(loaded, 19, &reference));
    assert(std::string(reference.class_name, reference.class_name_length) == "Helper");
    assert(std::string(reference.name, reference.name_length) == "answer");
    assert(std::string(reference.descriptor, reference.descriptor_length) == "()I");

    // A loaded class retains its archive and can resolve sibling classes later.
    opticraft_jar_close(archive);
    archive = nullptr;

    uint32_t code_length = 0;
    assert(opticraft_loaded_class_find_method(
        loaded, "main", "([Ljava/lang/String;)V", nullptr, &code_length));
    assert(code_length == 17);
    int32_t locals[1]={0},stack[2]={0,0};
    const auto main_result=opticraft_loaded_method_execute_i32(
        loaded,"main","([Ljava/lang/String;)V",locals,1,stack,2,30);
    assert(main_result.status==OPTI_EXEC_RETURN_VOID);
    assert(main_result.error==OPTI_EXEC_ERR_NONE);
    assert(main_result.steps==21);
    int32_t limited_locals[1]={0},limited_stack[2]={0,0};
    const auto limited=opticraft_loaded_method_execute_i32(
        loaded,"main","([Ljava/lang/String;)V",limited_locals,1,
        limited_stack,2,20);
    assert(limited.status==OPTI_EXEC_ERROR&&
           limited.error==OPTI_EXEC_ERR_STEP_LIMIT&&limited.steps==20);

    int32_t answer_stack[2]={0,0};
    const auto answer=opticraft_loaded_method_execute_i32(
        loaded,"answer","()I",nullptr,0,answer_stack,2,20);
    assert(answer.status==OPTI_EXEC_RETURN_I32&&answer.int_value==42);
    int32_t combine_locals[2]={7,2},combine_stack[2]={0,0};
    const auto combined=opticraft_loaded_method_execute_i32(
        loaded,"combine","(II)I",combine_locals,2,combine_stack,2,20);
    assert(combined.status==OPTI_EXEC_RETURN_I32&&combined.int_value==5);

    opticraft_loaded_class_destroy(loaded);
    std::remove(path.c_str());
    return 0;
}
