#include "mouse_input.hpp"
#include <stdexcept>
#include <iostream>
int main(){
    cc::MouseMotion mouse;
    auto check=[](auto actual,auto expected){if(actual!=expected)throw std::runtime_error("Mouse delta mismatch");};
    using Point=std::pair<int,int>;
    check(mouse.update(700,350,960,540),Point{0,0});
    check(mouse.update(600,250,960,540),Point{-100,-100});
    for(int n=0;n<10000;++n)check(mouse.update(600,250,960,540),Point{0,0});
    check(mouse.update(700,350,960,540),Point{100,100});
    check(mouse.update(960,540,960,540),Point{0,0});
    check(mouse.update(970,545,960,540),Point{10,5});
    mouse.reset();check(mouse.update(10,20,960,540),Point{0,0});
    check(mouse.update(99999,-99999,960,540),Point{1000,-1000});
    std::cout<<"Mouse event/focus/warp/stationary regression checks passed\n";
}
