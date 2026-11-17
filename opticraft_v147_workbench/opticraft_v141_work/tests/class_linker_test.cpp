#include "class_linker.hpp"
#include <cassert>
#include <iostream>
using namespace opti::runtime;
int main(){
 ClassInspection c; c.valid=true;c.this_class="game/Main";c.super_class="java/lang/Object";
 c.referenced_classes={"game/Util","java/lang/String","game/Util"};
 auto ok=prepare_class_link(c,[](const std::string& n){return n=="game/Util"||n=="java/lang/String";});
 assert(ok.valid&&ok.resolved.size()==2&&ok.missing.empty());
 auto missing=prepare_class_link(c,[](const std::string& n){return n=="game/Util";});
 assert(!missing.valid&&missing.missing.size()==1&&missing.missing[0]=="java/lang/String");
 assert(!prepare_class_link(c,ClassExists{}).valid);
 c.valid=false; assert(!prepare_class_link(c,[](const std::string&){return true;}).valid);
 std::cout<<"class linker preparation tests passed\n";
}
