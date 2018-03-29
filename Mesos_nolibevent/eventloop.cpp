#include "stdafx.h"
#include "eventloop.hpp"

namespace loop {
  
  // The `environment` variable stores the thread pool environment, which we
  // need to associate each Thread Pool object with the `cleanup_group`.
  // A `cleanup_group` associates all synchronization objects with a single
  // object, making it easy to wait for all of them and clean up.
  //
  // Note: we are using the default thread pool in Windows, so we don't
  // need to explictly create it.
  TP_CALLBACK_ENVIRON environment;
  PTP_CLEANUP_GROUP cleanup_group;

  std::once_flag flag;

  std::condition_variable exit_cv;
  std::mutex exit_mutex;
  bool exit = false;

  void EventLoop::initialize()
  {
    std::call_once(flag, []() {
      InitializeThreadpoolEnvironment(&environment);

      cleanup_group = CreateThreadpoolCleanupGroup();
      if (cleanup_group == NULL) {
        DWORD error = GetLastError();
        throw "Could not create cleanup group: " + std::to_string(error);
      }

      SetThreadpoolCallbackCleanupGroup(&environment, cleanup_group, NULL);
    });
  }

  void CALLBACK timer_callback(
    PTP_CALLBACK_INSTANCE instance,
    PVOID context,
    PTP_TIMER timer)
  {
    auto f = reinterpret_cast<std::function<void()>*>(context);
    (*f)();
    delete f;
    CloseThreadpoolTimer(timer);
  }
  

  void EventLoop::delay(
    const int duration,
    const std::function<void()>& function)
  {
    std::function<void()>* fptr = new std::function<void()>();
    *fptr = function;

    PTP_TIMER  timer =
      CreateThreadpoolTimer(timer_callback, (PVOID) fptr, &environment);

    if (timer == NULL) {
      DWORD error = GetLastError();
      throw "failed to create timer event: " + std::to_string(error) ; 
    }

    // If you give a positive value, then the timer call interprets it as absolute time.
    // A negative value is interpretted as relative time.
    // 0 is run immediately.
    ULARGE_INTEGER time;
    time.QuadPart = (ULONGLONG)(-duration * 10 * 1000 * 1000);

    FILETIME filetime;
    filetime.dwHighDateTime = time.HighPart;
    filetime.dwLowDateTime = time.LowPart;

    SetThreadpoolTimer(timer, &filetime, 0, 0);
  }


  double EventLoop::time()
  {
    FILETIME filetime;
    ULARGE_INTEGER time;

    GetSystemTimeAsFileTime(&filetime);

    // FILETIME isn't 8 byte aligned so they suggest not do cast to int64*.
    time.HighPart = filetime.dwHighDateTime;
    time.LowPart = filetime.dwLowDateTime;

    // Constant taken from here:
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms724228(v=vs.85).aspx
    if (time.QuadPart < 116444736000000000UL) {
      // We got a time before the epoch?
      throw "unvalid system time: " + std::to_string(time.QuadPart);
    }

    time.QuadPart -= 116444736000000000UL;

    // time has returns 100ns segments, so we divide to get seconds.
    return static_cast<double>(time.QuadPart) / 10000000;
  }


  void EventLoop::run()
  {
    // We don't actually need to run anything here since the thread pool is already running.
    // All we do is wait on a condition variable, so that this thread can clean up.
    std::unique_lock<std::mutex> lock(exit_mutex);
    exit_cv.wait(lock, []() { return exit; });
    
    CloseThreadpoolCleanupGroupMembers(cleanup_group, FALSE, NULL);
    CloseThreadpoolCleanupGroup(cleanup_group);
    DestroyThreadpoolEnvironment(&environment);
  }


  void EventLoop::stop()
  {
    std::lock_guard<std::mutex> lock(exit_mutex);
    exit = true;
    exit_cv.notify_one();
  }
}