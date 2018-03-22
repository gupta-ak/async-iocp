#include "stdafx.h"
#include "io.hpp"
#include "async_io.hpp"
#include "eventloop.hpp"

namespace async {
  struct Overlapped {
    OVERLAPPED o;
    std::promise<SSIZE_T> promise;
  };

  static void CALLBACK ioCallback(
    PTP_CALLBACK_INSTANCE Instance,
    PVOID Context,
    PVOID o,
    ULONG IoResult,
    ULONG_PTR NumberOfBytesTransferred,
    PTP_IO Io)
  {
    Overlapped* overlapped = reinterpret_cast<Overlapped*>(o);
    if (IoResult == NO_ERROR) {
      overlapped->promise.set_value(static_cast<SSIZE_T>(NumberOfBytesTransferred));
    }
    delete overlapped;
  }

  enum class WSAOverlappedType {
    SIZE_T,
    SOCKET,
    NONE
  };

  struct WSAOverlappedBase {
    virtual ~WSAOverlappedBase() = default;

    WSAOVERLAPPED o;
    WSAOverlappedType ot;
    WSABUF buf;
  };

  struct WSAOverlapped_SIZET : WSAOverlappedBase {
    std::promise<SSIZE_T> promise;
  };

  struct WSAOverlapped_SOCKET : WSAOverlappedBase {
    SOCKET result;
    std::promise<SOCKET> promise;
  };

  struct WSAOverlapped_VOID : WSAOverlappedBase {
    std::promise<void> promise;
  };

  static void CALLBACK socketCallback(
    PTP_CALLBACK_INSTANCE Instance,
    PVOID Context,
    PVOID Overlapped,
    ULONG IoResult,
    ULONG_PTR NumberOfBytesTransferred,
    PTP_IO Io)
  {
    WSAOverlappedBase* base = reinterpret_cast<WSAOverlappedBase*>(Overlapped);

    if (IoResult == NO_ERROR) {
      if (base->ot == WSAOverlappedType::SIZE_T) {
        reinterpret_cast<WSAOverlapped_SIZET*>(base)->promise.set_value(
          static_cast<size_t>(NumberOfBytesTransferred));
      } else if (base->ot == WSAOverlappedType::SOCKET) {
        WSAOverlapped_SOCKET* wsa_socket = reinterpret_cast<WSAOverlapped_SOCKET*>(base);
        wsa_socket->promise.set_value(wsa_socket->result);
      } else {
        reinterpret_cast<WSAOverlapped_VOID*>(base)->promise.set_value();
      }
    }
    delete base;
  }

  FileHandle::FileHandle(HANDLE h) : m_handle(h) {}

  std::future<SSIZE_T> FileHandle::readAsync(void* data, size_t size)
  {
    std::promise<SSIZE_T> promise;
    std::future<SSIZE_T> future = promise.get_future();

    DWORD bytesRead;
    if (ReadFile(m_handle, data, (DWORD)size, &bytesRead, NULL) == FALSE)
    {
      promise.set_value(-1);
    }
    else {
      promise.set_value(bytesRead);
    }
    return future;
  }

  std::future<SSIZE_T> FileHandle::writeAsync(const void* data, size_t size)
  {
    std::promise<SSIZE_T> promise;
    std::future<SSIZE_T> future = promise.get_future();

    DWORD bytesRead;
    if (WriteFile(m_handle, data, (DWORD)size, &bytesRead, NULL) == FALSE)
    {
      promise.set_value(-1);
    }
    else {
      promise.set_value(bytesRead);
    }
    return future;
  }

  void FileHandle::close()
  {
    CloseHandle(m_handle);
  }

  SocketHandle::SocketHandle(SOCKET s) : m_socket(s)
  {
    m_iocp = CreateThreadpoolIo(
      reinterpret_cast<HANDLE>(m_socket),
      &socketCallback,
      NULL,
      &loop::environment);
  }

  std::future<SSIZE_T> SocketHandle::readAsync(void* data, size_t size)
  {
    if (m_iocp == NULL) {
      std::promise<SSIZE_T> promise;
      std::future<SSIZE_T> future = promise.get_future();
      promise.set_value(-1);
      return future;
    }

    StartThreadpoolIo(m_iocp);

    WSAOverlapped_SIZET* overlapped = new WSAOverlapped_SIZET();
    overlapped->buf.buf = static_cast<char*>(data);
    overlapped->buf.len = static_cast<u_long>(size);

    std::future<SSIZE_T> future = overlapped->promise.get_future();

    int result = WSARecv(
      m_socket,
      &overlapped->buf,
      1,
      NULL,
      NULL,
      reinterpret_cast<OVERLAPPED*>(overlapped),
      NULL);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      delete overlapped;
      return future;
    }

    return future;
  }

  std::future<SSIZE_T> SocketHandle::writeAsync(const void* data, size_t size)
  {
    if (m_iocp == NULL) {
      std::promise<SSIZE_T> promise;
      std::future<SSIZE_T> future = promise.get_future();
      promise.set_value(-1);
      return future;
    }

    StartThreadpoolIo(m_iocp);

    WSAOverlapped_SIZET* overlapped = new WSAOverlapped_SIZET();
    overlapped->buf.buf = const_cast<char*>(static_cast<const char*>(data));
    overlapped->buf.len = static_cast<u_long>(size);

    std::future<SSIZE_T> future = overlapped->promise.get_future();

    int result = WSASend(
      m_socket,
      &overlapped->buf,
      1,
      NULL,
      NULL,
      reinterpret_cast<OVERLAPPED*>(overlapped),
      NULL);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      delete overlapped;
      return future;
    }

    return future;
  }

  void SocketHandle::close()
  {
    if (m_iocp != NULL) {
      CloseThreadpoolIo(m_iocp);
    }
    closesocket(m_socket);
  }

  PipeHandle::PipeHandle(HANDLE h) : m_handle(h)
  {
    m_iocp = CreateThreadpoolIo(
      reinterpret_cast<HANDLE>(m_handle),
      &ioCallback,
      NULL,
      &loop::environment);
  }


  std::future<SSIZE_T> PipeHandle::readAsync(void* data, size_t size)
  {
    if (m_iocp == NULL) {
      std::promise<SSIZE_T> promise;
      std::future<SSIZE_T> future = promise.get_future();
      promise.set_value(-1);
      return future;
    }

    StartThreadpoolIo(m_iocp);

    Overlapped* overlapped = new Overlapped();
    std::future<SSIZE_T> future = overlapped->promise.get_future();

    BOOL success = ReadFile(
      m_handle,
      data,
      (DWORD)size,
      NULL,
      reinterpret_cast<OVERLAPPED*>(overlapped));

    if (!success && GetLastError() != ERROR_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      delete overlapped;
      return future;
    }

    return future;
  }

  std::future<SSIZE_T> PipeHandle::writeAsync(const void* data, size_t size)
  {
    if (m_iocp == NULL) {
      std::promise<SSIZE_T> promise;
      std::future<SSIZE_T> future = promise.get_future();
      promise.set_value(-1);
      return future;
    }

    StartThreadpoolIo(m_iocp);

    Overlapped* overlapped = new Overlapped();
    std::future<SSIZE_T> future = overlapped->promise.get_future();

    BOOL success = WriteFile(
      m_handle,
      data,
      (DWORD)size,
      NULL,
      reinterpret_cast<OVERLAPPED*>(overlapped));

    if (!success && GetLastError() != ERROR_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      delete overlapped;
      return future;
    }

    return future;
  }

  void PipeHandle::close()
  {
    if (m_iocp != NULL) {
      CloseThreadpoolIo(m_iocp);
    }
    CloseHandle(m_handle);
  }

  Handle* createAsyncHandle(io::Handle* fd)
  {
    struct vistor {
      Handle* operator()(HANDLE h)
      {
        if (isOverlapped) {
          return new PipeHandle(h);
        }
        return new FileHandle(h);
      }

      Handle* operator()(SOCKET s)
      {
        return new SocketHandle(s);
      }
      bool isOverlapped;
    };

    std::variant<HANDLE, SOCKET> var = fd->dup();
    vistor v = { fd->isOverlapped() };
    return std::visit(v, var);
  }


  std::future<SSIZE_T> readAsync(
    Handle* fd,
    void* data,
    size_t size)
  {
    return fd->readAsync(data, size);
  }

  std::future<SSIZE_T> writeAsync(
    Handle* fd,
    const void* data,
    size_t size)
  {
    return fd->writeAsync(data, size);
  }

  void close(Handle* fd)
  {
    fd->close();
  }

  std::future<SSIZE_T> recv(
    SocketHandle* s,
    char* data,
    size_t size)
  {
    return s->readAsync(data, size);
  }

  std::future<SSIZE_T> send(
    SocketHandle* s,
    const char* data,
    size_t size)
  {
    return s->writeAsync(data, size);
  }

}
