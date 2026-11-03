#include <algorithm>
#include <chrono>
#include <windows.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#include <iiWebGPU/WebGPU.h>

#if !defined(min) || !defined(max)
#error "iiGPU WebGPU public headers must preserve the consuming application's macros"
#endif
