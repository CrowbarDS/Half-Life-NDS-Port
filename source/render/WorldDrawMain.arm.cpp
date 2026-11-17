// WorldDraw.itcm.arm.cpp's code in main memory, in a build to measure ITCM
// with (HLDS_ITCM_OFF, build.py); empty in any other.
#ifdef HLDS_ITCM_OFF
#define HLDS_WORLDDRAW_IN_MAIN 1
#include "render/WorldDraw.itcm.arm.cpp"
#endif
