// PS5X360E: the graphics driver's own messages in the game log.
//
// RADV (PS5_Mesa) writes to the standard error stream: among others, every 10 s
// a breakdown of where queue submission time goes. On the console that stream
// is not kept anywhere a player can send, so it is captured here: standard
// error becomes a pipe, and a thread hands each line to the log ("Driver: ...")
// and passes it on to where standard error went before.
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <thread>

#include "xenia/base/logging.h"

namespace xbox360ps5 {

void CaptureDriverMessages() {
  static bool started = false;
  if (started) return;
  int ends[2];
  if (pipe(ends) != 0) {
    XELOGW("Driver messages: no pipe, standard error is not logged");
    return;
  }
  const int previous = dup(STDERR_FILENO);
  std::fflush(stderr);
  // A writer never waits for the reader: a crash handler writing to standard
  // error must not block on a full pipe. Lines beyond the pipe's buffer are
  // dropped instead.
  fcntl(ends[1], F_SETFL, fcntl(ends[1], F_GETFL) | O_NONBLOCK);
  if (dup2(ends[1], STDERR_FILENO) < 0) {
    close(ends[0]);
    close(ends[1]);
    if (previous >= 0) close(previous);
    XELOGW("Driver messages: standard error could not be redirected");
    return;
  }
  close(ends[1]);
  std::setvbuf(stderr, nullptr, _IOLBF, 0);
  started = true;
  const int reader = ends[0];
  try {
    std::thread([reader, previous] {
      std::string pending;
      char buffer[1024];
      while (true) {
        const ssize_t got = read(reader, buffer, sizeof(buffer));
        if (got <= 0) break;
        if (previous >= 0) (void)write(previous, buffer, size_t(got));
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
    XELOGW("Driver messages: no reader thread, standard error is not logged");
    if (previous >= 0) {
      dup2(previous, STDERR_FILENO);
      close(previous);
    }
    close(reader);
    started = false;
    return;
  }
  XELOGI("Driver messages: standard error goes to this log");
}

}  // namespace xbox360ps5
