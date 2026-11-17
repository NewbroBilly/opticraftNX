#include "classfile_inspector.hpp"
#include "class_code_parser.hpp"
#include "bytecode_verifier.hpp"
#include "java_runtime_bootstrap.hpp"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace opti::runtime;
int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream in(argv[1],std::ios::binary);
    assert(in.good());
    std::vector<std::uint8_t> jar((std::istreambuf_iterator<char>(in)),{});
    auto inspection=inspect_jar(jar.data(),jar.size());
    assert(inspection.valid);
    assert(inspection.has_minecraft_main);
    std::size_t checked=0, code_parsed=0, methods_with_code=0, handlers=0, bytecode_verified=0;
    for(const auto& e:inspection.entries) {
        if(e.name.size()<6 || e.name.substr(e.name.size()-6)!=".class") continue;
        std::vector<std::uint8_t> bytes;
        std::string error;
        assert(extract_jar_entry(jar.data(),jar.size(),e,bytes,&error));
        auto code_cls=parse_class_code(bytes.data(),bytes.size());
        if(!code_cls.valid) {
            std::cerr<<"CODE PARSE FAIL "<<e.name<<": "<<code_cls.error<<"\\n";
            return 2;
        }
        code_parsed++;
        for(const auto& method:code_cls.methods) {
            methods_with_code++; handlers+=method.exception_table.size();
            auto bytecode=verify_bytecode_structure(method.code.data(),method.code.size());
            if(!bytecode.valid) {
                std::cerr<<"BYTECODE VERIFY FAIL "<<e.name<<"::"<<method.name<<method.descriptor
                         <<": "<<bytecode.error<<"\\n";
                return 3;
            }
            bytecode_verified++;
        }
        auto cls=inspect_classfile(bytes.data(),bytes.size());
        if(!cls.valid) {
            std::cerr<<"FAIL "<<e.name<<": "<<cls.error<<"\n";
            return 1;
        }
        assert(cls.major==49);
        assert(cls.this_class+".class"==e.name);
        checked++;
        if(e.name=="net/minecraft/client/Minecraft.class") {
            assert(cls.this_class=="net/minecraft/client/Minecraft");
            std::cout<<"MAIN class="<<cls.this_class<<" parent="<<cls.super_class
                     <<" methods="<<cls.methods_count<<" fields="<<cls.fields_count<<"\n";
        }
        if(checked==1) {
            auto corrupt=bytes;
            corrupt[0]=0;
            assert(!inspect_classfile(corrupt.data(),corrupt.size()).valid);
            corrupt=bytes;
            corrupt.pop_back();
            assert(!inspect_classfile(corrupt.data(),corrupt.size()).valid);
        }
    }
    assert(checked==1152);
    std::cout<<"PASS: parsed "<<checked<<" original Minecraft classfiles; Code classes="
             <<code_parsed<<" methods_with_code="<<methods_with_code
             <<" exception_handlers="<<handlers<<" bytecode_verified="<<bytecode_verified<<"\n";
}
