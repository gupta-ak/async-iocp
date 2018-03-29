#include "stdafx.h"
#include "io.hpp"
#include "async_io.hpp"
#include "eventloop.hpp"

namespace async {
  LPFN_CONNECTEX ConnectEx = NULL;

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
    } else {
      overlapped->promise.set_value(-1);
      std::cout << "ERROR in callback: " << IoResult << std::endl;
    }
    delete overlapped;
  }

  enum class WSAOverlappedType {
    SIZE_T,
    SOCKET,
    NONE
  };

  struct WSAOverlappedBase {
    WSAOVERLAPPED o;
    WSAOverlappedType ot;
    WSABUF buf;
  };

  struct WSAOverlapped_SIZET : WSAOverlappedBase {
    std::promise<SSIZE_T> promise;
  };

  struct WSAOverlapped_SOCKET : WSAOverlappedBase {
    SocketHandle* result;
    std::promise<SocketHandle*> promise;
  };

  struct WSAOverlapped_DWORD : WSAOverlappedBase {
    DWORD errorCode;
    std::promise<DWORD> promise;
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
    if (base->ot == WSAOverlappedType::SIZE_T) {
      WSAOverlapped_SIZET* wsa_socket = reinterpret_cast<WSAOverlapped_SIZET*>(base);
      if (IoResult == NO_ERROR) {
        wsa_socket->promise.set_value(static_cast<SSIZE_T>(NumberOfBytesTransferred));
      } else {
        wsa_socket->promise.set_value(static_cast<SSIZE_T>(-1));
      }
      delete wsa_socket;
    }
    else if (base->ot == WSAOverlappedType::SOCKET) {
      WSAOverlapped_SOCKET* wsa_socket = reinterpret_cast<WSAOverlapped_SOCKET*>(base);
      if (IoResult == NO_ERROR) {
        wsa_socket->promise.set_value(wsa_socket->result);
      } else {
        wsa_socket->result->close();
        delete wsa_socket->result;
        wsa_socket->promise.set_value(nullptr);
      }
      delete wsa_socket;
    }
    else {
      WSAOverlapped_DWORD* wsa_socket = reinterpret_cast<WSAOverlapped_DWORD*>(base);
      wsa_socket->promise.set_value(IoResult);
      delete wsa_socket;
    }

    if (IoResult != NO_ERROR) {
      std::cout << "ERROR in callback: " << IoResult << std::endl;
    }
  }


  FileHandle::FileHandle(HANDLE h) : m_handle(h) {}

  std::future<SSIZE_T> FileHandle::readAsync(void* data, size_t size) const
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

  std::future<SSIZE_T> FileHandle::writeAsync(const void* data, size_t size) const
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

  void FileHandle::close() const
  {
    CloseHandle(m_handle);
  }

  static BOOL loadConnect()
  {
    if (ConnectEx != NULL) {
      return TRUE;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
    {
      std::cout << "loadfunctions::socket error " << WSAGetLastError();
      return FALSE;
    }

    GUID connectex = WSAID_CONNECTEX;
    DWORD bytes;
    int res = WSAIoctl(
      s,
      SIO_GET_EXTENSION_FUNCTION_POINTER,
      &connectex,
      sizeof(connectex),
      &ConnectEx,
      sizeof(ConnectEx),
      &bytes,
      NULL,
      NULL);

    if (res != 0) {
      std::cout << "loadfunctions::ioctl error " << WSAGetLastError();
      return FALSE;
    }

    closesocket(s);
    return TRUE;
  }

  SocketHandle::SocketHandle(SOCKET s) : m_socket(s)
  {
    m_iocp = CreateThreadpoolIo(
      reinterpret_cast<HANDLE>(m_socket),
      &socketCallback,
      NULL,
      &loop::environment);
  }

  std::future<SSIZE_T> SocketHandle::readAsync(void* data, size_t size) const
  {
    if (m_iocp == NULL || m_socket == INVALID_SOCKET) {
      std::promise<SSIZE_T> promise;
      std::future<SSIZE_T> future = promise.get_future();
      promise.set_value(-1);
      return future;
    }

    StartThreadpoolIo(m_iocp);

    WSAOverlapped_SIZET* overlapped = new WSAOverlapped_SIZET();
    overlapped->o = { 0 };
    overlapped->ot = WSAOverlappedType::SIZE_T;
    overlapped->buf.buf = static_cast<char*>(data);
    overlapped->buf.len = static_cast<u_long>(size);

    std::future<SSIZE_T> future = overlapped->promise.get_future();

    DWORD lpflags = 0;
    int result = WSARecv(
      m_socket,
      &overlapped->buf,
      1,
      NULL,
      &lpflags,
      reinterpret_cast<OVERLAPPED*>(overlapped),
      NULL);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      delete overlapped;
      overlapped->promise.set_value(-1);
      return future;
    }

    return future;
  }

  std::future<SSIZE_T> SocketHandle::writeAsync(const void* data, size_t size) const
  {
    if (m_iocp == NULL || m_socket == INVALID_SOCKET) {
      std::promise<SSIZE_T> promise;
      std::future<SSIZE_T> future = promise.get_future();
      promise.set_value(-1);
      return future;
    }

    StartThreadpoolIo(m_iocp);

    WSAOverlapped_SIZET* overlapped = new WSAOverlapped_SIZET();
    overlapped->o = { 0 };
    overlapped->ot = WSAOverlappedType::SIZE_T;
    overlapped->buf.buf = const_cast<char*>(static_cast<const char*>(data));
    overlapped->buf.len = static_cast<u_long>(size);

    std::future<SSIZE_T> future = overlapped->promise.get_future();

    DWORD lpflags = 0;
    int result = WSASend(
      m_socket,
      &overlapped->buf,
      1,
      NULL,
      lpflags,
      reinterpret_cast<OVERLAPPED*>(overlapped),
      NULL);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      overlapped->promise.set_value(-1);
      delete overlapped;
      return future;
    }

    return future;
  }

  std::future<SSIZE_T> SocketHandle::sendfile(io::Handle* fd, off_t offset, size_t size) const
  {
    if (m_iocp == NULL || m_socket == INVALID_SOCKET) {
      std::promise<SSIZE_T> promise;
      std::future<SSIZE_T> future = promise.get_future();
      promise.set_value(-1);
      return future;
    }

    WSAOverlapped_SIZET* o = new WSAOverlapped_SIZET();
    std::future<SSIZE_T> future = o->promise.get_future();

    if (offset < 0) {
      o->promise.set_value(-1);
      delete o;
      return future;
    }

    StartThreadpoolIo(m_iocp);

    // Set offset values for sendfile
    o->o = { 0 };
    o->o.Offset = static_cast<DWORD>(offset);
    o->o.OffsetHigh = 0;
    o->ot = WSAOverlappedType::SIZE_T;

    BOOL success = TransmitFile(
      m_socket,
      fd->get(),
      static_cast<DWORD>(size),
      0,
      reinterpret_cast<OVERLAPPED*>(o),
      NULL,
      0);

    if (!success && WSAGetLastError() != WSA_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      o->promise.set_value(-1);
      delete o;
      return future;
    }

    return future;
  }

  std::future<SocketHandle*> SocketHandle::accept() const
  {
    if (m_iocp == NULL || m_socket == INVALID_SOCKET) {
      std::promise<SocketHandle*> promise;
      std::future<SocketHandle*> future = promise.get_future();
      promise.set_value(nullptr);
      return future;
    }

    WSAOverlapped_SOCKET* o = new WSAOverlapped_SOCKET();
    o->o = { 0 };
    o->ot = WSAOverlappedType::SOCKET;
    std::future<SocketHandle*> future = o->promise.get_future();

    // Create an accepting socket
    SOCKET acceptSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (acceptSocket == INVALID_SOCKET) {
      o->promise.set_value(nullptr);
      delete o;
      return future;
    }
    o->result = new SocketHandle(acceptSocket);

    char lpOutputBuf[2 * (sizeof(sockaddr_in) + 16)];
    int outBufLen = sizeof(lpOutputBuf);
    DWORD dwBytes;
    
    StartThreadpoolIo(m_iocp);
    BOOL result = AcceptEx(
      m_socket,
      acceptSocket,
      lpOutputBuf,
      0,
      sizeof(sockaddr_in) + 16,
      sizeof(sockaddr_in) + 16,
      &dwBytes,
      (OVERLAPPED*)o);
  
    if (!result && WSAGetLastError() != ERROR_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      o->result->close();
      delete o->result;
      o->promise.set_value(nullptr);
      delete o;
      return future;
    }

    return future;
  }

  std::future<DWORD> SocketHandle::connect(const sockaddr* addr, size_t addr_size) const
  {
    if (m_iocp == NULL || m_socket == INVALID_SOCKET) {
      std::promise<DWORD> promise;
      std::future<DWORD> future = promise.get_future();
      promise.set_value(~0);
      return future;
    }

    WSAOverlapped_DWORD* o = new WSAOverlapped_DWORD();
    o->o = { 0 };
    o->ot = WSAOverlappedType::NONE;
    std::future<DWORD> future = o->promise.get_future();

    // ConnectEx needs to bind first. Figure out how to do this
    // in a family independent way.
    struct sockaddr_in local_addr = { 0 };
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;

    int result = bind(m_socket, (struct sockaddr*) &local_addr, sizeof(local_addr));
    if (result == SOCKET_ERROR) {
      o->promise.set_value(~0);
      delete o;
      return future;
    }

    loadConnect();

    StartThreadpoolIo(m_iocp);
    BOOL success = ConnectEx(m_socket, addr, (int)addr_size, NULL, 0, NULL, (OVERLAPPED*) o);
    if (!success && WSAGetLastError() != ERROR_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      o->promise.set_value(~0);
      delete o;
      return future;
    }

    return future;
  }

  int SocketHandle::listen(int connections) const
  {
    int iResult = ::listen(m_socket, 1);
    if (iResult != 0) {
      return WSAGetLastError();
    }
    return iResult;
  }

  void SocketHandle::close() const
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

  std::future<SSIZE_T> PipeHandle::readAsync(void* data, size_t size) const
  {
    if (m_iocp == NULL) {
      std::promise<SSIZE_T> promise;
      std::future<SSIZE_T> future = promise.get_future();
      promise.set_value(-1);
      return future;
    }

    StartThreadpoolIo(m_iocp);

    Overlapped* overlapped = new Overlapped();
    overlapped->o = { 0 };
    std::future<SSIZE_T> future = overlapped->promise.get_future();

    BOOL success = ReadFile(
      m_handle,
      data,
      (DWORD)size,
      NULL,
      reinterpret_cast<OVERLAPPED*>(overlapped));

    if (!success && GetLastError() != ERROR_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      overlapped->promise.set_value(-1);
      delete overlapped;
      return future;
    }

    return future;
  }

  std::future<SSIZE_T> PipeHandle::writeAsync(const void* data, size_t size) const
  {
    if (m_iocp == NULL) {
      std::promise<SSIZE_T> promise;
      std::future<SSIZE_T> future = promise.get_future();
      promise.set_value(-1);
      return future;
    }

    StartThreadpoolIo(m_iocp);

    Overlapped* overlapped = new Overlapped();
    overlapped->o = { 0 };
    std::future<SSIZE_T> future = overlapped->promise.get_future();

    BOOL success = WriteFile(
      m_handle,
      data,
      (DWORD)size,
      NULL,
      reinterpret_cast<OVERLAPPED*>(overlapped));

    if (!success && GetLastError() != ERROR_IO_PENDING) {
      CancelThreadpoolIo(m_iocp);
      overlapped->promise.set_value(-1);
      delete overlapped;
      return future;
    }

    return future;
  }

  void PipeHandle::close() const
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
}
