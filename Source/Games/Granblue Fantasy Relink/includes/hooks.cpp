#include "..\..\Core\core.hpp"
#include "cbuffers.h"
#include "hooks.hpp"
#include "common.hpp"

#include <algorithm>
#include <initializer_list>

namespace
{
   bool MatchesBytes(uintptr_t address, std::initializer_list<uint8_t> expected)
   {
      MEMORY_BASIC_INFORMATION memory_info{};
      if (VirtualQuery(reinterpret_cast<const void*>(address), &memory_info, sizeof(memory_info)) == 0
          || memory_info.State != MEM_COMMIT || (memory_info.Protect & PAGE_GUARD) != 0
          || (memory_info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0)
         return false;

      const uintptr_t region_end = reinterpret_cast<uintptr_t>(memory_info.BaseAddress) + memory_info.RegionSize;
      if (address > region_end || expected.size() > region_end - address)
         return false;

      return std::equal(expected.begin(), expected.end(), reinterpret_cast<const uint8_t*>(address));
   }
}

bool ResolveGBFRAddresses()
{
   const HMODULE module = GetModuleHandleA(nullptr);
   const uintptr_t base = reinterpret_cast<uintptr_t>(module);
   if (base == 0)
      return false;

   wchar_t executable_path[MAX_PATH]{};
   if (GetModuleFileNameW(module, executable_path, MAX_PATH) == 0)
      return false;

   uint64_t file_version = 0;
   uint64_t product_version = 0;
   if (!System::GetDLLVersion(executable_path, file_version, product_version))
      return false;

   const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
   if (dos_header->e_magic != IMAGE_DOS_SIGNATURE || dos_header->e_lfanew <= 0)
      return false;

   const auto* nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos_header->e_lfanew);
   if (nt_headers->Signature != IMAGE_NT_SIGNATURE || nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
      return false;
   const uint16_t major = static_cast<uint16_t>(file_version >> 48);
   const uint16_t minor = static_cast<uint16_t>(file_version >> 32);
   const uint16_t patch = static_cast<uint16_t>(file_version >> 16);
   const uint16_t revision = static_cast<uint16_t>(file_version);
   const GBFRVersionAddressTable* addresses = nullptr;
   if (major == 2 && minor == 0 && patch == kGBFRVersion204.version_minor && revision == 0
       && nt_headers->FileHeader.TimeDateStamp == 0x6A6FFBA5 && nt_headers->OptionalHeader.SizeOfImage == 0x8217000)
      addresses = &kGBFRVersion204;
   else if (major == 2 && minor == 0 && patch == kGBFRVersion205.version_minor && revision == 0
            && nt_headers->FileHeader.TimeDateStamp == 0x6A7DA26E && nt_headers->OptionalHeader.SizeOfImage == 0x8217000)
      addresses = &kGBFRVersion205;
   else
      return false;

   const uintptr_t render_pipeline = base + addresses->initialize_dx11_rendering_pipeline;
   const uintptr_t jitter_write = base + addresses->jitter_write;
   const uintptr_t taa_init = base + addresses->temporal_aa_component_init;
   if (!MatchesBytes(render_pipeline, {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54})
       || !MatchesBytes(jitter_write, {0x55, 0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53})
       || !MatchesBytes(taa_init, {0x56, 0x48, 0x83, 0xEC, 0x40, 0xC5, 0xF8, 0x29, 0x7C, 0x24, 0x30}))
      return false;

   g_gbfr_version_minor = patch;
   g_resolved_addresses.initialize_dx11_rendering_pipeline = reinterpret_cast<void*>(render_pipeline);
   g_resolved_addresses.jitter_write_site = reinterpret_cast<void*>(jitter_write);
#ifdef PATCH_JITTER_TABLE_INIT
   g_resolved_addresses.temporal_aa_component_init = reinterpret_cast<void*>(taa_init);
#endif
   g_resolved_addresses.render_width = base + addresses->render_width;
   g_resolved_addresses.render_height = base + addresses->render_height;
   g_resolved_addresses.camera_index = base + addresses->camera_index;
   g_resolved_addresses.camera_table = base + addresses->camera_table;
   g_resolved_addresses.taa_settings_global = base + addresses->taa_settings_global;
   g_resolved_addresses.taa_running_flag = base + addresses->taa_running_flag;
   g_resolved_addresses.taa_render_scale_flag_ptr = base + addresses->taa_render_scale_flag_pointer;
   g_resolved_addresses.jitter_phase_counter = base + addresses->jitter_phase_counter;
   g_resolved_addresses.taa_reset_flag = base + addresses->taa_reset_flag;

   return true;
}

bool TryReadCameraJitter(float2& out_jitter)
{
   if (g_resolved_addresses.camera_index != 0 && g_resolved_addresses.camera_table != 0)
   {
      const int camera_idx = *reinterpret_cast<const int*>(g_resolved_addresses.camera_index);
      if (camera_idx < 0 || camera_idx > 0xb)
         return false;

      const uintptr_t camera = *reinterpret_cast<const uintptr_t*>(
         g_resolved_addresses.camera_table + static_cast<size_t>(camera_idx) * 8);
      if (camera == 0)
         return false;

      const uintptr_t projection_ptr = *reinterpret_cast<const uintptr_t*>(camera + kCameraProjectionDataOffset);
      if (projection_ptr == 0)
         return false;

      out_jitter.x = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterXOffset);
      out_jitter.y = *reinterpret_cast<const float*>(projection_ptr + kProjectionJitterYOffset);
      return true;
   }
   return false;
}

void OnJitterWrite(safetyhook::Context& ctx)
{
   // v2.0.3+: Jitter stored in TAA component table at [rcx + 8*(phase&0x3F) + 0x28]
   // ctx.rcx = TemporalAntiAliasingComponent*, phase counter is global
   const uint8_t phase = *reinterpret_cast<const uint8_t*>(g_resolved_addresses.jitter_phase_counter);
   const uintptr_t jit_addr = ctx.rcx + 8 * (phase & 0x3F) + 0x28;
   g_hook_globals.table_jitter_x_bits.store(
      *reinterpret_cast<const uint32_t*>(jit_addr), std::memory_order_release);
   g_hook_globals.table_jitter_y_bits.store(
      *reinterpret_cast<const uint32_t*>(jit_addr + 4), std::memory_order_release);
   g_hook_globals.table_jitter_valid.store(true, std::memory_order_release);
#ifdef PATCH_JITTER_TABLE_INIT
   const uint8_t phase_idx = *reinterpret_cast<const uint8_t*>(g_resolved_addresses.jitter_phase_counter);
   g_hook_globals.cached_jitter_phase_idx.store(phase_idx, std::memory_order_release);
#endif
}

bool TryReadTableJitter(float2& out_jitter)
{
   if (!g_hook_globals.table_jitter_valid.load(std::memory_order_acquire))
      return false;
   const uint32_t x_bits = g_hook_globals.table_jitter_x_bits.load(std::memory_order_relaxed);
   const uint32_t y_bits = g_hook_globals.table_jitter_y_bits.load(std::memory_order_relaxed);
   std::memcpy(&out_jitter.x, &x_bits, sizeof(float));
   std::memcpy(&out_jitter.y, &y_bits, sizeof(float));
   return true;
}

#ifdef PATCH_JITTER_TABLE_INIT
constexpr std::array<float2, JITTER_PHASES> precomputed_jitters = []()
{
   std::array<float2, JITTER_PHASES> entries{};
   for (unsigned int i = 0; i < entries.size(); i++)
      entries[i] = float2{SR::HaltonSequence(i, 2), SR::HaltonSequence(i, 3)};
   return entries;
}();

bool TryReadTableJitterFromCounter(float2& out_jitter)
{
   // Use phase index cached by OnJitterWrite to avoid off-by-one from g_frame_counter
   // (incremented on game-logic thread one frame ahead of render thread).
   if (!g_hook_globals.table_jitter_valid.load(std::memory_order_acquire))
      return false;
   const uint8_t phase_idx = g_hook_globals.cached_jitter_phase_idx.load(std::memory_order_acquire);
   out_jitter = precomputed_jitters[phase_idx % JITTER_PHASES];
   return true;
}

static void __fastcall Hooked_TemporalAntiAliasingComponentInit(void* self)
{
   g_taa_init_hook.call<void>(self);
   auto* table = reinterpret_cast<float2*>(reinterpret_cast<uint8_t*>(self) + kTAAJitterTableOffset);
   for (size_t i = 0; i < kTAAJitterTableCount; i++)
      table[i] = precomputed_jitters[i % JITTER_PHASES];
}
#endif

void PatchJitterPhases()
{
   // No-op when PATCH_JITTER_TABLE_INIT is defined — the init hook handles phase control.
   // When disabled, patches phase mask bytes in the game's jitter write function.
   static_assert((JITTER_PHASES & (JITTER_PHASES - 1)) == 0, "JITTER_PHASES must be a power of 2");
   static_assert(JITTER_PHASES >= 1 && JITTER_PHASES <= 64, "JITTER_PHASES must be between 1 and 64");

#ifndef PATCH_JITTER_TABLE_INIT
   constexpr uint8_t mask = static_cast<uint8_t>(JITTER_PHASES - 1);
   const uintptr_t patch_addrs[2] = {
      g_resolved_addresses.jitter_phase_mask_cl_imm,
      g_resolved_addresses.jitter_phase_mask_eax_imm,
   };
   for (uintptr_t addr : patch_addrs)
   {
      if (addr == 0)
         continue;
      auto* byte_ptr = reinterpret_cast<uint8_t*>(addr);
      DWORD old_protect;
      VirtualProtect(byte_ptr, 1, PAGE_EXECUTE_READWRITE, &old_protect);
      *byte_ptr = mask;
      VirtualProtect(byte_ptr, 1, old_protect, &old_protect);
   }
#endif
}

bool IsTAARunningThisFrame()
{
   // During pause/unpause transitions the settings object can be rebuilt temporarily.
   // Keep and return the last known-good value when reads are transiently unavailable.
   static std::atomic<bool> s_last_taa_running{false};

   const bool last_known = s_last_taa_running.load(std::memory_order_acquire);

   // v2.0.3+: taa_running_flag is a pointer to the TAA running flag byte.
   // Verified in TemporalAntiAliasingComponent::trans (RVA 0x215F9C0):
   //   mov rax, cs:qword_147371338  (RVA 0x7371338) — load pointer
   //   cmp byte ptr [rax], 0        — read byte at target address
   //   cmp byte ptr [rax], 1        — if not 1, skip render scale adjustment
   // Requires double-dereference: load pointer, then read byte.
   const uintptr_t pointer_addr = g_resolved_addresses.taa_running_flag;
   if (pointer_addr == 0)
      return last_known;

   __try
   {
      const uintptr_t target_addr = *reinterpret_cast<const uintptr_t*>(pointer_addr);
      const bool taa_running = (*reinterpret_cast<const uint8_t*>(target_addr) & 1) != 0;
      s_last_taa_running.store(taa_running, std::memory_order_release);
      return taa_running;
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {
      return last_known;
   }
}

bool TryGetSettingsObject(uintptr_t& out_settings_obj)
{
   // 2.0.4/2.0.5 taa_settings_global is a 16-byte inline buffer, not a pointer.
   out_settings_obj = 0;
   return false;
}

void* GetVTableFunction(void* obj, size_t index)
{
   void** vtable = *reinterpret_cast<void***>(obj);
   return vtable[index];
}

bool TryReadTAAResetFlag()
{
   const uintptr_t addr = g_resolved_addresses.taa_reset_flag;
   if (addr == 0)
      return false;
   __try
   {
      return (*reinterpret_cast<const uint8_t*>(addr) & 1) != 0;
   }
   __except (EXCEPTION_EXECUTE_HANDLER)
   {
      return false;
   }
}

// GBFR_InitializeDX11RenderingPipeline is called every frame from a single caller.
// It has a dimension cache (g_rtDimensionCache) that gates RT recreation — cache is
// written with the incoming args before RT creation, so substituting args here means
// the render dims are cached automatically and RT recreation only fires on dim change.
//
// screen_width/screen_height are read by the caller from g_outputWidth/g_outputHeight,
// so they always carry the current output resolution. We substitute render-scaled dims
// into the trampoline call and write them to g_renderWidth/g_renderHeight — the globals
// the frame graph reads to decide whether the temporal upscale path runs.
// g_outputWidth/g_outputHeight are never modified.
static char __fastcall Hooked_InitializeDX11RenderingPipeline(int screen_width, int screen_height)
{
   int render_w = screen_width;
   int render_h = screen_height;

   DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
   if (device_data && screen_width > 0 && screen_height > 0)
   {
      // screen_width/height ARE the output dims — keep output_resolution current every frame.
      device_data->output_resolution.x = static_cast<float>(screen_width);
      device_data->output_resolution.y = static_cast<float>(screen_height);

      const float scale = render_scale;
      const double aspect_ratio = static_cast<double>(screen_width) / screen_height;
      auto render_dims = Math::FindClosestIntegerResolutionForAspectRatio(
         screen_width * static_cast<double>(scale),
         screen_height * static_cast<double>(scale),
         aspect_ratio);
      device_data->render_resolution.x = static_cast<float>(render_dims[0]);
      device_data->render_resolution.y = static_cast<float>(render_dims[1]);

      render_w = static_cast<int>((std::max)(1u, render_dims[0]));
      render_h = static_cast<int>((std::max)(1u, render_dims[1]));

      // Keep g_renderWidth/g_renderHeight in sync with the args we pass to the trampoline.
      // TAA component reads these at +0x6B81058/+0x6B8105C to decide whether to run
      // the temporal upscale path. Without this write, render == output and TUPDrawPass skips.
      if (g_resolved_addresses.render_width != 0 && g_resolved_addresses.render_height != 0)
      {
         *reinterpret_cast<int*>(g_resolved_addresses.render_width) = render_w;
         *reinterpret_cast<int*>(g_resolved_addresses.render_height) = render_h;
      }
   }

   // Pass render dims to the game — g_outputWidth/g_outputHeight are not touched.
   return g_rt_creation_hook.unsafe_call<char>(render_w, render_h);
}

void PatchSceneBufferInHook(
   ID3D11DeviceContext1* pContext,
   ID3D11Buffer* pBuffer,
   UINT firstConstant,
   UINT numConstants)
{
   DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
   ID3D11Device* native_device = g_native_device_ptr.load(std::memory_order_acquire);
   if (!device_data || !native_device)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: device_data or native_device null");
      return;
   }

   auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);

   constexpr UINT scene_buffer_size = sizeof(cbSceneBuffer);
   const UINT scene_buffer_constants = scene_buffer_size / 16;
   if (numConstants < scene_buffer_constants)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: numConstants too small");
      return;
   }

   auto it = device_data->native_compute_shaders.find(CompileTimeStringHash("GBFR Patch SceneBuffer"));
   if (it == device_data->native_compute_shaders.end() || !it->second)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: compute shader not found");
      return;
   }

   if (!game_device_data.scratch_scene_buffer || !game_device_data.scratch_scene_buffer_uav)
   {
      ASSERT_ONCE_MSG(false, "PatchSceneBufferInHook: scratch buffer or UAV missing");
      return;
   }

   DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
   compute_state_stack.Cache(pContext, device_data->uav_max_count);

   if (device_data->luma_instance_data)
   {
      ID3D11Buffer* luma_cbs[] = {device_data->luma_instance_data.get()};
      pContext->CSSetConstantBuffers(8, 1, luma_cbs);
   }

   {
      ID3D11Buffer* cbs[] = {pBuffer};
      UINT firsts[] = {firstConstant};
      UINT counts[] = {numConstants};
      pContext->CSSetConstantBuffers1(0, 1, cbs, firsts, counts);
   }

   ID3D11UnorderedAccessView* uavs[] = {game_device_data.scratch_scene_buffer_uav.get()};
   pContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

   pContext->CSSetShader(it->second.get(), nullptr, 0);
   pContext->Dispatch(1, 1, 1);

   ID3D11UnorderedAccessView* null_uavs[] = {nullptr};
   pContext->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);

   D3D11_BOX src_box = {};
   src_box.left = 0;
   src_box.right = scene_buffer_size;
   src_box.top = 0;
   src_box.bottom = 1;
   src_box.front = 0;
   src_box.back = 1;
   pContext->CopySubresourceRegion(
      pBuffer,
      0,
      firstConstant * 16,
      0,
      0,
      game_device_data.scratch_scene_buffer.get(),
      0,
      &src_box);

   compute_state_stack.Restore(pContext);
}
