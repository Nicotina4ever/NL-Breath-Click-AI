// SPDX-License-Identifier: MPL-2.0
#include <studio_engine.h>
#include <iostream>
int main() {
    bc::studio::TimeMap map;
    map.originalFrames=100;map.editedFrames=80;
    map.segments={{0,20,0,20},{40,100,20,80}};
    if(map.mapOriginal(60)!=40 || map.mapEdited(40)!=60)return 1;
    std::cout<<"NL Audio Suite SDK: shared TimeMap engine OK\n";
    return 0;
}
