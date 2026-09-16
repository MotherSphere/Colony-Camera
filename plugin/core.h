#pragma once
#include <cstddef>
#include <cstdint>
struct CameraState { float position[3]; float native[3]; std::uint32_t initialized; };
struct CameraFrame { float position[3]; float rotation[4]; float dt; std::uint32_t reset; };
struct CameraProfile { float offset[3]; float half_life; float max_lag; };
struct CameraConfig { CameraProfile profiles[3]; std::uint32_t keys[3]; std::uint32_t enabled; };
static_assert(sizeof(CameraState) == 28 && sizeof(CameraFrame) == 36);
static_assert(sizeof(CameraProfile) == 20 && sizeof(CameraConfig) == 76);
extern "C" CameraState cc_step(CameraState, CameraFrame, CameraProfile);
extern "C" CameraConfig cc_defaults();
extern "C" std::uint32_t cc_parse_config(const unsigned char*, std::size_t, CameraConfig*);
