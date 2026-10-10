// SPDX-License-Identifier: MIT
#include "xbox360ps5/title_switch.hpp"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>
#include "xenia/base/cvar.h"
#include "xenia/config.h"
#include "xenia/base/utf8.h"

DECLARE_uint32(draw_resolution_scale_threshold);
DECLARE_bool(readback_resolve_async);
DECLARE_bool(resolve_sync_only_watched);
#if XE_PLATFORM_PS5
extern "C" int sceSystemServiceLoadExec(const char*, const char**);
#endif
namespace xbox360ps5 {
namespace {
namespace fs = std::filesystem;
fs::path launched_game;
std::string module_override;
fs::path PendingFile() {
#if XE_PLATFORM_PS5
  return "/download0/xbox360ps5/next-title.txt";
#else
  return fs::temp_directory_path() / "xbox360ps5-next-title.txt";
#endif
}
}

void SetLaunchedGame(const fs::path& game) { launched_game = game; }

bool RelaunchInto(const std::string& guest_path, const std::vector<uint8_t>& launch_data) {
  std::error_code error;
  fs::remove(PendingFile(), error);
  if (!guest_path.empty() && !launched_game.empty()) {
    // Three lines: the game to mount, the executable in it, the launch data in hex.
    std::ofstream output(PendingFile(), std::ios::trunc);
    output << launched_game.string() << "\n" << guest_path << "\n";
    static const char* const hex = "0123456789abcdef";
    for (uint8_t byte : launch_data) output << hex[byte >> 4] << hex[byte & 15];
    output << "\n";
    output.close();
    XELOGW("Title switch: restarting into {} ({} bytes of launch data)", guest_path, launch_data.size());
  } else {
    XELOGW("Title switch: the game returned to the dashboard; restarting into the launcher");
  }
#if XE_PLATFORM_PS5
  {
    // Called from a guest thread while others still log: the logger is left running.
    const int refused = sceSystemServiceLoadExec("/app0/eboot.bin", nullptr);
    XELOGE("Title switch: restart refused {:08X}", unsigned(refused));
  }
#else
  if (std::getenv("XBOX360PS5_SWITCH_EXIT")) {
    std::fflush(nullptr);
    std::_Exit(0);
  }
#endif
  return false;
}

bool TakePendingLaunch(PendingLaunch& launch) {
  std::ifstream input(PendingFile());
  std::string game, data;
  if (!std::getline(input, game) || !std::getline(input, launch.module) || game.empty()) return false;
  std::getline(input, data);
  input.close();
  std::error_code error;
  fs::remove(PendingFile(), error);
  launch.game = game;
  launch.launch_data.clear();
  for (size_t at = 0; at + 1 < data.size(); at += 2)
    launch.launch_data.push_back(uint8_t(std::stoul(data.substr(at, 2), nullptr, 16)));
  return fs::exists(launch.game, error);
}

void SetModuleOverride(const std::string& module) { module_override = module; }

std::string TakeModuleOverride() { return std::exchange(module_override, std::string()); }

namespace {
std::vector<fs::path> GameConfigFiles(const std::string& title_id) {
#if XE_PLATFORM_PS5
  const fs::path folders[] = {"/app0/assets/game-configs", "/download0/xbox360ps5/game-configs"};
#else
  const fs::path folders[] = {"dist/PPSA50011/assets/game-configs"};
#endif
  std::vector<fs::path> files;
  for (const auto& folder : folders) {
    std::error_code error;
    const fs::path file = folder / (title_id + ".config.toml");
    if (fs::is_regular_file(file, error)) files.push_back(file);
  }
  return files;
}
}
bool HasGameConfig(const std::string& title_id) {
  return !title_id.empty() && !GameConfigFiles(title_id).empty();
}
void LoadGameConfigs(const std::string& title_id) {
#if XE_PLATFORM_PS5
  const fs::path folders[] = {"/app0/assets/game-configs", "/download0/xbox360ps5/game-configs"};
#else
  const fs::path folders[] = {"dist/PPSA50011/assets/game-configs"};
#endif
  // PS5X360E: built-in emulator settings for titles that need them, set for
  // every title (so one game's value doesn't carry over to the next) before
  // the files below, which can still change them.
  // Need for Speed: The Run at 2x: its brightness measurement (a chain of ever
  // smaller copies of the picture, read back by the CPU) came out wrong
  // upscaled, so scenes were far darker than at 1x. Keeping targets up to 320
  // pixels wide native fixed it (user, alpha.39).
  const bool nfs_the_run = xe::utf8::lower_ascii(title_id) == "4541094a";
  cvars::draw_resolution_scale_threshold = nfs_the_run ? 320 : 0;
  // NFS The Run reads small render output (its brightness measurement) about
  // 100 times a second; waiting for the GPU on each read took 30-270 ms a
  // second of its threads at 2x. Copied for it on the GPU instead, the reads
  // see output up to a frame old, and no early submissions are needed for
  // them (alpha.41).
  cvars::readback_resolve_async = nfs_the_run;
  cvars::resolve_sync_only_watched = nfs_the_run;
  if (!cvar::ConfigVars) return;
  for (const auto& folder : folders) {
    const fs::path file = folder / (title_id + ".config.toml");
    std::error_code error;
    if (!fs::is_regular_file(file, error)) continue;
    // PS5X360E: Xenia Edge has no ReadGameConfig; apply the file's [Category]
    // name = value entries as Edge applies a game config, over the settings.
    toml::parse_result table;
    try {
      table = ParseFile(file);
    } catch (const std::exception& e) {
      XELOGE("Game config {}: {}", file.string(), e.what());
      continue;
    }
    size_t applied = 0;
    for (auto& entry : *cvar::ConfigVars) {
      auto* var = entry.second;
      const auto node = table.at_path(toml::path(var->category() + "." + var->name()));
      if (!node) continue;
      var->LoadGameConfigValue(node.node());
      ++applied;
      XELOGW("Game config: {}.{} from {}", var->category(), var->name(), file.string());
    }
    XELOGW("Game config: {} applied {} setting(s)", file.string(), applied);
  }
}
}
