// PS5X360E: the graphics driver's own messages in the game log.
//
// RADV (PS5_Mesa) writes to the standard error stream: among others, every 10 s
// a breakdown of where queue submission time goes. On the console that stream
// is not kept anywhere a player can send. The console refuses to put a pipe in
// its place (dup2 onto descriptor 2 failed, alpha.48), so standard error is
// reopened onto a file instead (as the native runtime probe does), and a thread
// follows that file, handing each new line to the log ("Driver: ...").
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "xenia/base/logging.h"

namespace xbox360ps5 {

void CaptureDriverMessages(const std::filesystem::path& directory) {
  static bool started = false;
  if (started || directory.empty()) return;
  const std::string path = (directory / "driver-messages.txt").string();
  std::fflush(stderr);
  if (!std::freopen(path.c_str(), "w", stderr)) {
    XELOGW("Driver messages: standard error could not be reopened on {}", path);
    return;
  }
  std::setvbuf(stderr, nullptr, _IOLBF, 0);
  const int reader = open(path.c_str(), O_RDONLY);
  if (reader < 0) {
    XELOGW("Driver messages: {} could not be read back; they stay in that file", path);
    return;
  }
  started = true;
  try {
    std::thread([reader] {
      std::string pending;
      char buffer[1024];
      while (true) {
        const ssize_t got = read(reader, buffer, sizeof(buffer));
        if (got < 0) break;
        if (got == 0) {
          // At the end of what was written so far: look again shortly.
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          continue;
        }
        pending.append(buffer, size_t(got));
        for (size_t end; (end = pending.find('\n')) != std::string::npos;
             pending.erase(0, end + 1)) {
          if (end) XELOGW("Driver: {}", std::string_view(pending.data(), end));
        }
        if (pending.size() > 4096) {
          XELOGW("Driver: {}", pending);
          pending.clear();
        }
      }
    }).detach();
  } catch (...) {
    close(reader);
    started = false;
    XELOGW("Driver messages: no reader thread; they stay in {}", path);
    return;
  }
  XELOGW("Driver messages: standard error goes to this log (through {})", path);
}

}  // namespace xbox360ps5
