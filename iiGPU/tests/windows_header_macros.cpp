#include <chrono>
#include <windows.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#include <iiGPU/GPU.h>

#if !defined(min) || !defined(max)
#error "iiGPU public headers must preserve the consuming application's macros"
#endif
