#include "class_linker.hpp"
#include <algorithm>
namespace opti::runtime {
LinkResult prepare_class_link(const ClassInspection& c,const ClassExists& exists){
 LinkResult out;
 if(!c.valid){out.error="cannot link structurally invalid class";return out;}
 if(c.this_class.empty()){out.error="class has no internal name";return out;}
 if(!exists){out.error="class resolver callback is empty";return out;}
 std::vector<std::string> refs=c.referenced_classes;
 if(!c.super_class.empty() && c.super_class!="java/lang/Object") refs.push_back(c.super_class);
 refs.erase(std::remove(refs.begin(),refs.end(),c.this_class),refs.end());
 std::sort(refs.begin(),refs.end()); refs.erase(std::unique(refs.begin(),refs.end()),refs.end());
 for(const auto& name:refs){if(exists(name))out.resolved.push_back(name);else out.missing.push_back(name);}
 out.valid=out.missing.empty();
 if(!out.valid)out.error="one or more symbolic class dependencies are unresolved";
 return out;
}
}
