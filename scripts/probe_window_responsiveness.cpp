// Probe one owned X11/XWayland window using the same ping protocol as the WM.
// Compile with: c++ -std=c++20 -O2 scripts/probe_window_responsiveness.cpp -lX11 -o <output>
// Usage: <output> <window-id> <seconds> [close]
#include <X11/Xlib.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {
bool windowGone = false;
int handleError(Display *, XErrorEvent *error) {
  if (error->error_code == BadWindow) {
    windowGone = true;
  }
  return 0;
}
void sendProtocol(Display *display, Window window, Atom protocols, Atom command,
                  long token) {
  XEvent message{};
  message.xclient.type = ClientMessage;
  message.xclient.display = display;
  message.xclient.window = window;
  message.xclient.message_type = protocols;
  message.xclient.format = 32;
  message.xclient.data.l[0] = command;
  message.xclient.data.l[1] = token;
  message.xclient.data.l[2] = window;
  XSendEvent(display, window, False, NoEventMask, &message);
  XFlush(display);
}
}
int main(int argc, char **argv) {
  if (argc < 3) {
    return 2;
  }
  auto *display = XOpenDisplay(nullptr);
  if (!display) {
    return 2;
  }
  XSetErrorHandler(handleError);
  const auto window = Window(std::strtoul(argv[1], nullptr, 0));
  const auto root = DefaultRootWindow(display);
  const auto protocols = XInternAtom(display, "WM_PROTOCOLS", False);
  const auto ping = XInternAtom(display, "_NET_WM_PING", False);
  XSelectInput(display, root, SubstructureNotifyMask);
  using Clock = std::chrono::steady_clock;
  const auto deadline = Clock::now() + std::chrono::seconds(std::atoi(argv[2]));
  std::vector<double> latencies;
  unsigned timeouts = 0;
  long token = 100000;
  while (Clock::now() < deadline && !windowGone) {
    const auto sent = Clock::now();
    sendProtocol(display, window, protocols, ping, ++token);
    bool replied = false;
    while (Clock::now() - sent < std::chrono::seconds(2) && !windowGone) {
      while (XPending(display)) {
        XEvent event{};
        XNextEvent(display, &event);
        if (event.type == ClientMessage && event.xclient.window == root &&
            event.xclient.message_type == protocols &&
            event.xclient.data.l[0] == long(ping) && event.xclient.data.l[1] == token) {
          replied = true;
        }
      }
      if (replied) {
        latencies.push_back(std::chrono::duration<double, std::milli>(Clock::now() - sent).count());
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!replied && !windowGone) {
      ++timeouts;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  std::sort(latencies.begin(), latencies.end());
  std::cout << "{\"replies\":" << latencies.size() << ",\"timeouts_over_2s\":" << timeouts
            << ",\"maximum_reply_ms\":" << (latencies.empty() ? 0 : latencies.back()) << "}\n";
  if (argc > 3 && !windowGone) {
    sendProtocol(display, window, protocols, XInternAtom(display, "WM_DELETE_WINDOW", False), CurrentTime);
  }
  XCloseDisplay(display);
  return timeouts ? 1 : 0;
}
