#pragma once

#include "hook_constants.hpp"
#include "safetyhook.hpp"

#include <mutex>
#include <shared_mutex>

struct GBFRResolvedAddresses
{
   void* initialize_dx11_rendering_pipeline = nullptr;
   void* jitter_write_site = nullptr;
#ifdef PATCH_JITTER_TABLE_INIT
   void* temporal_aa_component_init = nullptr;
#endif

   uintptr_t render_width = 0;
   uintptr_t render_height = 0;

   // CameraIndex + CameraTable mechanism used by verified 2.0.4/2.0.5 builds.
   uintptr_t camera_index = 0;
   uintptr_t camera_table = 0;
   uintptr_t taa_settings_global = 0;
   // Pointer address (qword); double-dereference to read the TAA running flag byte.
   uintptr_t taa_running_flag = 0;
   // Pointer to the settings struct with the render-scale flag at +0x65.
   uintptr_t taa_render_scale_flag_ptr = 0;
   uintptr_t jitter_phase_counter = 0;
   uintptr_t taa_reset_flag = 0;
};

struct GBFRHookGlobals
{
   SafetyHookInline rt_creation_hook;
   safetyhook::MidHook jitter_write_hook;
#ifdef PATCH_JITTER_TABLE_INIT
   SafetyHookInline taa_init_hook;
#endif

   std::shared_mutex device_state_mutex;
   std::mutex hook_install_mutex;
   std::atomic<DeviceData*> device_data_ptr = nullptr;
   std::atomic<ID3D11Device*> native_device_ptr = nullptr;
   std::atomic<uint32_t> table_jitter_x_bits{0};
   std::atomic<uint32_t> table_jitter_y_bits{0};
   std::atomic<bool> table_jitter_valid{false};
#ifdef PATCH_JITTER_TABLE_INIT
   // Cached by OnJitterWrite — phase index captured at the moment the camera receives its jitter.
   std::atomic<uint8_t> cached_jitter_phase_idx{0};
#endif
};

inline GBFRHookGlobals g_hook_globals;
inline auto& g_rt_creation_hook = g_hook_globals.rt_creation_hook;
inline auto& g_jitter_write_hook = g_hook_globals.jitter_write_hook;
#ifdef PATCH_JITTER_TABLE_INIT
inline auto& g_taa_init_hook = g_hook_globals.taa_init_hook;
#endif
inline auto& g_device_state_mutex = g_hook_globals.device_state_mutex;
inline auto& g_hook_install_mutex = g_hook_globals.hook_install_mutex;
inline auto& g_device_data_ptr = g_hook_globals.device_data_ptr;
inline auto& g_native_device_ptr = g_hook_globals.native_device_ptr;
inline GBFRResolvedAddresses g_resolved_addresses;
inline uint16_t g_gbfr_version_minor = 0;

bool ResolveGBFRAddresses();

bool TryReadCameraJitter(float2& out_jitter);
void OnJitterWrite(safetyhook::Context& ctx);
bool TryReadTableJitter(float2& out_jitter);
void PatchJitterPhases();
#ifdef PATCH_JITTER_TABLE_INIT
bool TryReadTableJitterFromCounter(float2& out_jitter);
void __fastcall Hooked_TemporalAntiAliasingComponentInit(void* self);
#endif
bool IsTAARunningThisFrame();
bool TryGetSettingsObject(uintptr_t& out_settings_obj);
void* GetVTableFunction(void* obj, size_t index);
bool TryReadTAAResetFlag();

char __fastcall Hooked_InitializeDX11RenderingPipeline(int screen_width, int screen_height);
void PatchSceneBufferInHook(
   ID3D11DeviceContext1* pContext,
   ID3D11Buffer* pBuffer,
   UINT firstConstant,
   UINT numConstants);