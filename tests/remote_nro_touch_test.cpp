#include "../projects/daemon/src/remote_nro_touch.hpp"
#include <cassert>
#include <vector>
#include <iostream>

static std::vector<int> contacts;
static std::vector<HidTouchState> points;
static std::vector<u64> sleeps;
static int sets, unsets, exits, inits, failSet, failUnset;
static Result failInit;
static void reset() {
    contacts.clear(); points.clear(); sleeps.clear();
    sets = unsets = exits = inits = failSet = failUnset = 0; failInit = 0;
}
extern "C" {
Result hiddbgInitialize() { ++inits; return failInit; }
void hiddbgExit() { ++exits; }
Result hiddbgSetTouchScreenAutoPilotState(const HidTouchState* p, s32 count) {
    contacts.push_back(count); points.push_back(*p);
    return ++sets == failSet ? 123U : 0;
}
Result hiddbgUnsetTouchScreenAutoPilotState() { return ++unsets <= failUnset ? 456U : 0; }
void svcSleepThread(u64 ns) { sleeps.push_back(ns); }
}
int main() {
    using namespace switchu::remote_touch;
    reset(); assert(gesture(1279,719,100,false,0)==0);
    assert((contacts==std::vector<int>{0,1,0}));
    assert(points[1].x==1279 && points[1].y==719 && points[1].finger_id==1);
    assert((sleeps==std::vector<u64>{50'000'000,100'000'000,50'000'000}));
    assert(unsets==1 && exits==1); assert(shutdown()==0 && inits==1);
    reset(); assert(gesture(10,20,80,true,120)==0);
    assert((contacts==std::vector<int>{0,1,0,1,0}));
    assert((sleeps==std::vector<u64>{50'000'000,80'000'000,120'000'000,80'000'000,50'000'000}));
    reset(); assert(gesture(640,360,5000,false,0)==0);
    assert(sleeps[1]==5'000'000'000ULL && unsets==1 && exits==1);
    for (int fail=1;fail<=5;++fail) {
        reset(); failSet=fail; assert(gesture(10,20,100,true,120)==123U);
        assert(unsets==1 && exits==1 && sets==fail); // no gesture replay
    }
    reset(); failInit=99; assert(gesture(0,0,100,false,0)==99);
    assert(sets==0 && unsets==0 && exits==0);
    reset(); failUnset=1; assert(gesture(0,0,100,false,0)==456U);
    assert(unsets==2 && exits==1); assert(shutdown()==0 && inits==1);
    reset(); failUnset=2; assert(gesture(0,0,100,false,0)==456U);
    assert(unsets==2 && exits==1); assert(shutdown()==0);
    assert(unsets==3 && exits==2); // OFF recovers an uncertain cleanup
    reset(); assert(gesture(1280,0,100,false,0)!=0);
    assert(gesture(0,720,100,false,0)!=0 && gesture(0,0,5001,false,0)!=0);
    assert(gesture(0,0,301,true,120)!=0 && gesture(0,0,100,true,49)!=0);
    assert(inits==0);
    std::cout << "touch gesture, timing, release and cleanup fault tests passed\n";
}
