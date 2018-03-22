#pragma once

#include "stdafx.h"

namespace loop {
  extern TP_CALLBACK_ENVIRON environment;

  // The interface that must be implemented by an event management
  // system. This is a class to cleanly isolate the interface and so
  // that in the future we can support multiple implementations.
  class EventLoop
  {
  public:
    // Initializes the event loop.
    static void initialize();

    // Invoke the specified function in the event loop after the
    // specified duration.
    // TODO(bmahler): Update this to use rvalue references.
    static void delay(
      const int duration,
      const std::function<void()>& function);

    // Returns the current time w.r.t. the event loop.
    static double time();

    // Runs the event loop.
    static void run();

    // Asynchronously tells the event loop to stop and then returns.
    static void stop();
  };

}