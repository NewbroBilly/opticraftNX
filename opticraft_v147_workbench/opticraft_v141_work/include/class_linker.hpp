#pragma once
#include "classfile_inspector.hpp"
#include <functional>
#include <string>
#include <vector>
namespace opti::runtime {
struct LinkResult { bool valid=false; std::string error; std::vector<std::string> resolved; std::vector<std::string> missing; };
using ClassExists = std::function<bool(const std::string&)>;
LinkResult prepare_class_link(const ClassInspection& inspected,const ClassExists& exists);
}
