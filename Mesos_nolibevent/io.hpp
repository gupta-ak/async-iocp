#pragma once

#include "stdafx.h"

// Stout-like api with blocking IO.

namespace io {
  class Handle {
  public:
    virtual SSIZE_T read(void *data, size_t size) const = 0;

    virtual SSIZE_T write(void *data, size_t size) const = 0;

    virtual void close() const = 0;

    virtual bool isOverlapped() const = 0;

    virtual std::variant<HANDLE, SOCKET> dup() const = 0;

  };

  class FileHandle : public Handle {
  public:
    FileHandle(HANDLE h) : m_handle(h) {}

    SSIZE_T read(void *data, size_t size) const override
    {
      DWORD bytesRead;
      BOOL success = ReadFile(
        static_cast<HANDLE>(m_handle),
        data,
        static_cast<DWORD>(size),
        &bytesRead,
        NULL);

      if (!success) {
        return -1;
      }
      return static_cast<SSIZE_T>(bytesRead);
    }

    SSIZE_T write(void *data, size_t size) const override
    {
      DWORD bytesWritten;
      BOOL success = WriteFile(
        static_cast<HANDLE>(m_handle),
        data,
        static_cast<DWORD>(size),
        &bytesWritten,
        NULL);

      if (!success) {
        return -1;
      }

      return static_cast<SSIZE_T>(bytesWritten);
    }

    void close() const override
    {
      CloseHandle(m_handle);
    }

    bool isOverlapped() const override
    {
      return false;
    }

    std::variant<HANDLE, SOCKET> dup() const override
    {
      HANDLE target;
      BOOL success = DuplicateHandle(
        GetCurrentProcess(),
        m_handle,
        GetCurrentProcess(),
        &target,
        0,
        FALSE,
        DUPLICATE_SAME_ACCESS);

      if (!success) {
        return std::variant<HANDLE, SOCKET>();
      }

      return target;
    }

  protected:
    HANDLE m_handle;
  };

  class SocketHandle : public Handle {
  public:
    SocketHandle(SOCKET s) : m_socket(s) { }

    SSIZE_T read(void* data, size_t datalen) const override
    {
      int result = recv(m_socket, static_cast<char*>(data), static_cast<int>(datalen), 0);
      if (result == SOCKET_ERROR) {
        return -1;
      }
      return result;
    }

    SSIZE_T write(void* data, size_t datalen) const override
    {
      int result = recv(m_socket, static_cast<char*>(data), static_cast<int>(datalen), 0);
      if (result == SOCKET_ERROR) {
        return -1;
      }
      return result;
    }

    void close() const override
    {
      ::closesocket(m_socket);
    }
    
    bool isOverlapped() const override
    {
      return true;
    }

    std::variant<HANDLE, SOCKET> dup() const override
    {
      WSAPROTOCOL_INFO  info;
      int result = WSADuplicateSocket(m_socket, GetCurrentProcessId(), &info);
      if (result != SOCKET_ERROR)
      {
        return INVALID_HANDLE_VALUE;
      }

      SOCKET s = WSASocket(
        info.iAddressFamily,
        info.iSocketType,
        info.iProtocol,
        &info,
        0,
        WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);

      if (s == INVALID_SOCKET)
      {
        return std::variant<HANDLE, SOCKET>();
      }

      return s;
    }

  protected:
    SOCKET m_socket;
  };

  class PipeHandle : public Handle {
  public:
    PipeHandle(HANDLE h, bool isOverlapped)
      : m_handle(h), m_overlapped(isOverlapped) { }

    SSIZE_T read(void* data, size_t datalen) const override
    {
      OVERLAPPED o = { 0 };

      BOOL success = ReadFile(
        m_handle,
        data,
        static_cast<DWORD>(datalen),
        NULL,
        &o);

      if (!success && GetLastError() != ERROR_IO_PENDING) {
        return -1;
      }

      DWORD bytesRead;
      success = GetOverlappedResult(m_handle, &o, &bytesRead, TRUE);
      if (!success) {
        return -1;
      }

      return static_cast<SSIZE_T>(bytesRead);
    }

    SSIZE_T write(void* data, size_t datalen) const override {
      OVERLAPPED o = { 0 };

      BOOL success = WriteFile(
        m_handle,
        data,
        static_cast<DWORD>(datalen),
        NULL,
        &o);

      if (!success && GetLastError() != ERROR_IO_PENDING) {
        return -1;
      }

      DWORD bytesWritten;
      success = GetOverlappedResult(m_handle, &o, &bytesWritten, TRUE);
      if (!success) {
        return -1;
      }

      return static_cast<SSIZE_T>(bytesWritten);
    }

    void close() const override
    {
      CloseHandle(m_handle);
    }

    bool isOverlapped() const override
    {
      return m_overlapped;
    }

    std::variant<HANDLE, SOCKET> dup() const override
    {
      HANDLE target;
      BOOL success = DuplicateHandle(
        GetCurrentProcess(),
        m_handle,
        GetCurrentProcess(),
        &target,
        0,
        FALSE,
        DUPLICATE_SAME_ACCESS);

      if (!success) {
        return std::variant<HANDLE, SOCKET>();
      }

      return target;
    }

  protected:
    HANDLE m_handle;
    bool m_overlapped;
  };

  inline Handle* open(
    const wchar_t* lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwCreationDisposition)
  {
    HANDLE h = CreateFileW(
      lpFileName,
      dwDesiredAccess,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL,
      dwCreationDisposition,
      FILE_ATTRIBUTE_NORMAL,
      NULL);

    if (h == INVALID_HANDLE_VALUE) {
      return nullptr;
    }
    return new FileHandle(h);
  }

  inline SSIZE_T read(
    Handle* fd,
    void* data,
    size_t size)
  {
    return fd->read(data, size);
  }

  inline SSIZE_T write(
    Handle* fd,
    void* data,
    size_t size)
  {
    return fd->write(data, size);
  }

  inline void close(Handle* fd)
  {
    fd->close();
  }

  inline std::array<Handle*, 2> pipe(DWORD readFlags, DWORD writeFlags)
  {
    UUID uuid;
    RPC_STATUS status = UuidCreate(&uuid);
    if (status != RPC_S_OK && status != RPC_S_UUID_LOCAL_ONLY) {
      return { nullptr, nullptr };
    }

    wchar_t* str;
    status = UuidToStringW(&uuid, (RPC_WSTR*)&str);
    if (status != RPC_S_OK) {
      return { nullptr, nullptr };
    }

    wchar_t name[MAX_PATH] = L"\\\\.\\pipe\\mesos-";
    size_t namelen = wcslen(name);
    wcscpy_s(name + namelen, MAX_PATH - namelen, str);

    RpcStringFreeW((RPC_WSTR*)&str);

    HANDLE readHandle, writeHandle;
    readHandle = CreateNamedPipeW(
      name,
      PIPE_ACCESS_INBOUND | readFlags,
      PIPE_TYPE_BYTE | PIPE_WAIT,
      1,
      0,
      0,
      0,
      NULL);

    if (readHandle == INVALID_HANDLE_VALUE) {
      return { nullptr, nullptr };
    }

    writeHandle = CreateFileW(
      name,
      GENERIC_WRITE,
      0,
      NULL,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | writeFlags,
      NULL);

    if (writeHandle == INVALID_HANDLE_VALUE) {
      DWORD error = GetLastError();
      CloseHandle(readHandle);
      return { nullptr, nullptr };
    }

    bool isReadOverlapped = readFlags & FILE_FLAG_OVERLAPPED;
    bool isWriteOverlapped = writeFlags & FILE_FLAG_OVERLAPPED;

    return { new PipeHandle(readHandle, isReadOverlapped), new PipeHandle(writeHandle, isWriteOverlapped) };
  }

  // Sockets
  inline Handle* socket(int af, int type, int protocol)
  {
    SOCKET s = ::socket(af, type, protocol);
    if (s == INVALID_SOCKET) {
      return nullptr;
    }
    return new SocketHandle(s);
  }

  inline void closesocket(Handle* s)
  {
    s->close();
  }
}