// SPDX-License-Identifier: MIT
#pragma once

namespace xbox360ps5 {
enum class Renderer : int { kVulkan = 0, kOpenGL = 1 };

// Only an executable linked with the real guest backend may enable this.
// The standalone EGL/compiler probe must never define this feature macro.
inline constexpr bool OpenGLGameRendererBuilt() {
#if defined(PS5X360_OPENGL_GAME_RENDERER) && PS5X360_OPENGL_GAME_RENDERER
  return true;
#else
  return false;
#endif
}

inline constexpr const char* RendererName(int selected) {
  return selected == int(Renderer::kOpenGL) ? "OpenGL" : "Vulkan";
}

inline constexpr bool RendererAvailable(int selected) {
  return selected == int(Renderer::kVulkan) ||
         (selected == int(Renderer::kOpenGL) && OpenGLGameRendererBuilt());
}
}  // namespace xbox360ps5
