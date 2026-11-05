#pragma once

#include <Windows.h>

#include <filesystem>

#include "core/core_api.h"

namespace reshade::api {
struct swapchain;
struct swapchain_desc;
enum class device_api;
}  // namespace reshade::api

namespace mohpcf::display {

using RequestBorderlessFn = void (*)(HWND window, bool on);

// `binaries` = the game's Binaries folder (where the add-on and MoH2010.PCFix.ini live).
void initialize(const std::filesystem::path& binaries, RequestBorderlessFn request_borderless);

// ReShade events.
bool on_create_swapchain(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd);
void on_init_swapchain(reshade::api::swapchain* swapchain, bool resize);

// Panel tabs.
void draw_display_tab();
void draw_mouse_tab();

}  // namespace mohpcf::display
