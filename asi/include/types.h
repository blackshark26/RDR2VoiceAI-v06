#pragma once
#include <windows.h>
#include <cstdint>

using Any       = uint64_t;
using Void      = uint64_t;
using ScrHandle = int;
using Entity    = ScrHandle;
using Ped       = ScrHandle;
using Player    = unsigned int;
using Hash      = unsigned int;
using BOOL      = int;

#define ALIGN8 __declspec(align(8))

struct Vector3 {
    ALIGN8 float x;
    ALIGN8 float y;
    ALIGN8 float z;
};
static_assert(sizeof(Vector3) == 24, "Vector3 size mismatch");
