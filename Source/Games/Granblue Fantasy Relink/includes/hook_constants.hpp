#pragma once

#include <cstdint>

// ============================================================
// Granblue Fantasy Relink - Version-Specific Address Constants
// ============================================================
// Runtime-selected versions:
//   - 2.0.4: SHA-256 f827f3c13caa90b290fab2fe7e28165a80448fde0a3f7a96d79dac6b8343ff2a
//   - 2.0.5: SHA-256 7189b958ff0fe5238cea28a2939ffdad6e3a9acb14dd274a9fcc8e7e275bd175
// Older tables remain below for binary research but are not selected by this build.
// ============================================================

struct GBFRVersionAddressTable
{
   uint16_t version_minor;
   uintptr_t initialize_dx11_rendering_pipeline;
   uintptr_t jitter_write;
   uintptr_t temporal_aa_component_init;
   uintptr_t render_width;
   uintptr_t render_height;
   uintptr_t camera_index;
   uintptr_t camera_table;
   uintptr_t taa_settings_global;
   uintptr_t taa_running_flag;
   uintptr_t taa_render_scale_flag_pointer;
   uintptr_t jitter_phase_counter;
   uintptr_t taa_reset_flag;
};

constexpr GBFRVersionAddressTable kGBFRVersion204 = {
   4, 0x007F4420, 0x02160960, 0x021607B0, 0x06B822D8, 0x06B822DC,
   0x0701F560, 0x054BC3A0, 0x0703DD10, 0x073725B8, 0x07031030,
   0x0703D6B0, 0x07372290};

constexpr GBFRVersionAddressTable kGBFRVersion205 = {
   5, 0x007F4760, 0x02160A60, 0x021608B0, 0x06B822D8, 0x06B822DC,
   0x0701F780, 0x054BC3A0, 0x0703DF30, 0x07372848, 0x07031250,
   0x0703D8D0, 0x07372520};

// ============================================================
// Common offsets (same across all versions)
// ============================================================
constexpr size_t kVSSetConstantBuffers1_VTableIndex = 119;
constexpr uintptr_t kCameraProjectionDataOffset = 0x60;
constexpr uintptr_t kProjectionJitterXOffset = 0x940;
constexpr uintptr_t kProjectionJitterYOffset = 0x944;
// Jitter table: 64 entries × 8 bytes (float2), offset 0x28 from TAA component*
//   Unchanged across all versions (1.3.2/2.0.2/2.0.3/2.0.4)
constexpr uintptr_t kTAAJitterTableOffset = 0x28;
constexpr uintptr_t kTAAJitterPhaseIndexOffset = 0x24;
constexpr size_t kTAAJitterTableCount = 64;
