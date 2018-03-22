#pragma once

#include "stdafx.h"
#include "io.hpp"

namespace async {
  class Handle {
  public:
    virtual std::future<SSIZE_T> readAsync(
      void* data,
      size_t size) = 0;

    virtual std::future<SSIZE_T> writeAsync(
      const void* data,
      size_t size) = 0;

    virtual void close() = 0;
  };

  // Works like a regular handle.
  class FileHandle : public Handle {
  public:
    FileHandle(HANDLE h);

    std::future<SSIZE_T> readAsync(void* data, size_t size) override;

    std::future<SSIZE_T> writeAsync(const void* data, size_t size) override;

    void close() override;

  protected:
    HANDLE m_handle;
  };

  class SocketHandle : public Handle {
  public:
    SocketHandle(SOCKET s);

    std::future<SSIZE_T> readAsync(void* data, size_t size) override;

    std::future<SSIZE_T> writeAsync(const void* data, size_t size) override;

    void close() override;

  protected:
    SOCKET m_socket;
    PTP_IO m_iocp;
  };

  class PipeHandle : public Handle {
  public:
    PipeHandle(HANDLE h);

    std::future<SSIZE_T> readAsync(void* data, size_t size) override;

    std::future<SSIZE_T> writeAsync(const void* data, size_t size) override;

    void close() override;

  protected:
    HANDLE m_handle;
    PTP_IO m_iocp;
  };
  
  Handle* createAsyncHandle(io::Handle* fd);
  
  std::future<SSIZE_T> readAsync(
    Handle* fd,
    void* data,
    size_t size);

  std::future<SSIZE_T> writeAsync(
    Handle* fd,
    const void* data,
    size_t size);

  void close(Handle* fd);

  /* std::future<io::Handle*> accept(
    io::Handle* s);

  std::future<void> connect(
    io::Handle* s,
    const struct sockaddr* name,
    int namelen); */

  std::future<SSIZE_T> recv(
    SocketHandle* s,
    char* data,
    size_t size);

  std::future<SSIZE_T> send(
    SocketHandle* s,
    const char* data,
    size_t size);

  /*std::future<size_t> sendfile(
    io::Handle* s,
    io::Handle* fd,
    off_t offset,
    size_t size);*/
}