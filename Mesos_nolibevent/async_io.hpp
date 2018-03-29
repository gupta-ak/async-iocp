#pragma once

#include "stdafx.h"
#include "io.hpp"

namespace async {
  class Handle {
  public:
    ~Handle() {}

    virtual std::future<SSIZE_T> readAsync(
      void* data,
      size_t size) const = 0;

    virtual std::future<SSIZE_T> writeAsync(
      const void* data,
      size_t size) const = 0;

    virtual void close() const = 0;
  };

  // Works like a regular handle.
  class FileHandle : public Handle {
  public:
    FileHandle(HANDLE h);

    std::future<SSIZE_T> readAsync(void* data, size_t size) const override;

    std::future<SSIZE_T> writeAsync(const void* data, size_t size) const override;

    void close() const override;

  protected:
    HANDLE m_handle;
  };

  class SocketHandle : public Handle {
  public:
    SocketHandle();

    SocketHandle(SOCKET s);

    std::future<SSIZE_T> readAsync(void* data, size_t size) const override;

    std::future<SSIZE_T> writeAsync(const void* data, size_t size) const override;

    std::future<SocketHandle*> accept() const;

    std::future<DWORD> connect(const sockaddr* addr, size_t addr_size) const;

    std::future<SSIZE_T> sendfile(io::Handle* fd, off_t offset, size_t size) const;

    int listen(int connections) const;

    void close() const override;

  protected:
    SOCKET m_socket;
    PTP_IO m_iocp;
  };

  class PipeHandle : public Handle {
  public:
    PipeHandle(HANDLE h);

    std::future<SSIZE_T> readAsync(void* data, size_t size) const override;

    std::future<SSIZE_T> writeAsync(const void* data, size_t size) const override;

    void close() const override;

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
}