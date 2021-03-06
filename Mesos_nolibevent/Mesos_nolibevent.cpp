// Mesos_nolibevent.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "eventloop.hpp"
#include "io.hpp"
#include "async_io.hpp"

#pragma comment(lib, "Rpcrt4.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

#define DEFAULT_PORT 27015

int init_wsa()
{
  WSADATA wsaData;
  int iResult;

  // Initialize Winsock
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    printf("WSAStartup failed: %d\n", iResult); 
  }
  return iResult;
}

void close_wsa()
{
  WSACleanup();
}

void timer_func(int i, int max)
{
  std::cout << "The time is " << (uint64_t)loop::EventLoop::time() << " (should be "
    << time(NULL) << ") second since the epoch\n" << std::endl;

  std::cout << "Scheduling timer " << i << " to run after "
    << max - i << " seconds..." << std::endl;

  loop::EventLoop::delay(max - i, [i]() {
    std::cout << "Hi from thread " << i << "!" << std::endl;
  });
}

void file_sync_read_func(const std::wstring& file)
{
  std::wcout << L"Beginning sync read of " << file << std::endl;
  
  auto deleter = [](io::Handle* h) {
    h->close();
    delete h;
  };

  std::unique_ptr<io::Handle, decltype(deleter)> readFile(
    io::open(file.c_str(), GENERIC_READ, OPEN_EXISTING),
    deleter);

  if (readFile.get() == nullptr) {
    std::wcout << L"IO OPEN FAILED: " << GetLastError() << std::endl;
    throw;
  }

  std::wstring writtenFile = file + L"_copy.txt";
  std::unique_ptr<io::Handle, decltype(deleter)> writeFile(
    io::open(writtenFile.c_str(), GENERIC_WRITE, CREATE_ALWAYS),
    deleter);

  if (writeFile.get() == nullptr) {
    std::wcout << L"IO OPEN FAILED: " << GetLastError() << std::endl;
    throw;
  }

  for (;;) {
    char buf[1024] = { 0 };
    SSIZE_T bytes = io::read(readFile.get(), buf, sizeof(buf));
    if (bytes == -1) {
      std::wcout << L"IO READ FAILED: " << GetLastError() << std::endl;
      throw;
    }
    else if (bytes == 0) {
      break;
    }

    SSIZE_T wBytes = io::write(writeFile.get(), buf, (size_t)bytes);
    if (wBytes == -1) {
      std::wcout << L"IO WRITE FAILED: " << GetLastError() << std::endl;
      throw;
    }
    else if (bytes != wBytes) {
      std::wcout << L"IO READ: " << bytes << " IO WRITE: " << wBytes << std::endl;
      throw;
    }
  }
  std::wcout << std::endl << "-------END OF FILE--------" << std::endl;
}

void file_async_read_func(const std::wstring& file)
{
  std::wcout << L"Beginning (fake) async read of " << file << std::endl;

  auto deleter = [](async::Handle* h) {
    h->close();
    delete h;
  };

  io::Handle* hRead = io::open(file.c_str(), GENERIC_READ, OPEN_EXISTING);
  if (hRead == nullptr) {
    std::wcout << L"IO OPEN FAILED: " << GetLastError() << std::endl;
    throw;
  }

  std::unique_ptr<async::Handle, decltype(deleter)> hAsyncRead(
    async::createAsyncHandle(hRead),
    deleter);

  io::close(hRead);
  delete hRead;

  std::wstring writtenFile = file + L"_copy_async.txt";
  io::Handle* hWrite = io::open(writtenFile.c_str(), GENERIC_WRITE, CREATE_ALWAYS);
  if (hWrite == nullptr) {
    std::wcout << L"IO OPEN FAILED: " << GetLastError() << std::endl;
    throw;
  }
  std::unique_ptr<async::Handle, decltype(deleter)> hAsyncWrite(
    async::createAsyncHandle(hWrite),
    deleter);

  io::close(hWrite);
  delete hWrite;

  std::wcout << L"Contents of file:" << std::endl;
  for (;;) {
    char buf[1024] = { 0 };
    std::future<SSIZE_T> future = async::readAsync(hAsyncRead.get(), buf, sizeof(buf));
    future.wait();
    SSIZE_T bytes = future.get();
    if (bytes == -1) {
      std::wcout << L"IO READ FAILED: " << GetLastError() << std::endl;
      throw;
    }
    else if (bytes == 0) {
      break;
    }

    std::future<SSIZE_T> future2 = async::writeAsync(hAsyncWrite.get(), buf, (size_t)bytes);
    future2.wait();
    SSIZE_T bytes2 = future2.get();
    if (bytes2 == -1) {
      std::wcout << L"IO Write FAILED: " << GetLastError() << std::endl;
      throw;
    }
    else if (bytes != bytes2) {
      std::wcout << L"IO READ: " << bytes << " IO WRITE: " << bytes2 << std::endl;
    }

  }

  std::wcout << std::endl << "-------END OF FILE--------" << std::endl;
}

void test_timers()
{
  int max = 5;
  for (int i = 0; i < max; i++) {
    timer_func(i, max);
  }
  Sleep(max * 1000);
}

void test_files()
{
  std::vector<std::wstring> files =
  {

  };
  for (const std::wstring& file : files) {
    file_sync_read_func(file);
    file_async_read_func(file);
  }
}

void test_pipes()
{
  auto deleter = [](async::Handle* h) {
    h->close();
    delete h;
  };

  std::vector<std::unique_ptr<async::Handle, decltype(deleter)>> readFds;
  std::vector<std::unique_ptr<char[]>> readBufs;
  std::vector<std::future<SSIZE_T>> readFutures;

  std::vector<std::unique_ptr<async::Handle, decltype(deleter)>> writeFds;
  std::vector<std::unique_ptr<char[]>> writeBufs;
  std::vector<std::future<SSIZE_T>> writeFutures;
  
  int max = 10;
  size_t bufsize = 1024;
  for (int i = 0; i < max; i++)
  {
    std::array<io::Handle*, 2> pipes = io::pipe(FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED);
    if (pipes[0] == nullptr || pipes[1] == nullptr) {
      std::cout << "FAILED creating pipes: " << ::GetLastError() << std::endl;
      throw;
    }

    // Make async handles
    std::unique_ptr<async::Handle, decltype(deleter)> readPipe(
      async::createAsyncHandle(pipes[0]),
      deleter);

    std::unique_ptr<async::Handle, decltype(deleter)> writePipe(
      async::createAsyncHandle(pipes[1]),
      deleter);

    pipes[0]->close();
    pipes[1]->close();
    delete pipes[0];
    delete pipes[1];

    // Try reading. It shouldn't block
    std::cout << "Attempting a async read on pipe " << i << std::endl;
    std::unique_ptr<char[]> buf(new char[bufsize]());
    std::future<SSIZE_T> readFuture = async::readAsync(readPipe.get(), buf.get(), bufsize);

    readFds.push_back(std::move(readPipe));
    readBufs.push_back(std::move(buf));
    readFutures.push_back(std::move(readFuture));
    writeFds.push_back(std::move(writePipe));
  }
  
  // Now do async write
  int i = 0;
  for (const auto& fd : writeFds) {
    // Try writing. This might not block anyway because there is a reader
    // on the other side.
    std::cout << "Attempting a async write on pipe " << i << std::endl;
    
    std::string data = std::string("Hello World! From call ") + std::to_string(i);
    std::unique_ptr<char[]> buf(new char[bufsize]());
    memcpy(buf.get(), data.c_str(), data.size() + 1);

    std::future<SSIZE_T> writeFuture = async::writeAsync(fd.get(), buf.get(), data.size() + 1);
    i++;

    writeBufs.push_back(std::move(buf));
    writeFutures.push_back(std::move(writeFuture));
  }

  // Now, wait on the futures
  for (i = 0; i < max; i++) {
    std::future<SSIZE_T>& wFuture = writeFutures[i];
    wFuture.wait();
    SSIZE_T wBytes = wFuture.get();
    if (wBytes == -1) {
      std::cout << "Async Write FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::future<SSIZE_T>& rFuture = readFutures[i];
    rFuture.wait();
    SSIZE_T rBytes = rFuture.get();
    if (rBytes == -1) {
      std::cout << "Async Read FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::cout << "Pipe " << i << " write -> read as completed. Now checking results" << std::endl;
    if (rBytes != wBytes) {
      std::cout << "Pipe " << i << " does not match in read / write bytes = " << rBytes << " / " << wBytes << std::endl;
      throw;
    }

    bool failed = false;
    for (int j = 0; j < bufsize; j++) {
      if (readBufs[i][j] != writeBufs[i][j]) {
        failed = true;
        std::cout << "Pipe " << i << " differs on write buf on index " << j << ". "
          << "Read = " << readBufs[i][j] << " vs Write = " << writeBufs[i][j] << std::endl;
      }
    }
    
    if (failed) {
      throw;
    }
    std::cout << "Pipe " << i << " passed." << std::endl;
    std::cout << readBufs[i].get() << std::endl;
  }
}


void test_pipe_write_first()
{
  auto deleter = [](async::Handle* h) {
    h->close();
    delete h;
  };

  std::vector<std::unique_ptr<async::Handle, decltype(deleter)>> readFds;
  std::vector<std::unique_ptr<char[]>> readBufs;
  std::vector<std::future<SSIZE_T>> readFutures;

  std::vector<std::unique_ptr<async::Handle, decltype(deleter)>> writeFds;
  std::vector<std::unique_ptr<char[]>> writeBufs;
  std::vector<std::future<SSIZE_T>> writeFutures;

  int max = 10;
  size_t bufsize = 1024;
  for (int i = 0; i < max; i++)
  {
    std::array<io::Handle*, 2> pipes = io::pipe(FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED);
    if (pipes[0] == nullptr || pipes[1] == nullptr) {
      std::cout << "FAILED creating pipes: " << ::GetLastError() << std::endl;
      throw;
    }

    // Make async handles
    std::unique_ptr<async::Handle, decltype(deleter)> readPipe(
      async::createAsyncHandle(pipes[0]),
      deleter);

    std::unique_ptr<async::Handle, decltype(deleter)> writePipe(
      async::createAsyncHandle(pipes[1]),
      deleter);

    pipes[0]->close();
    pipes[1]->close();
    delete pipes[0];
    delete pipes[1];

    // Try writing. It shouldn't block
    std::cout << "Attempting a async write on pipe " << i << std::endl;
    
    std::string data = std::string("Hello World! From call ") + std::to_string(i);
    std::unique_ptr<char[]> buf(new char[bufsize]());
    memcpy(buf.get(), data.c_str(), data.size() + 1);

    std::future<SSIZE_T> writeFuture = async::writeAsync(writePipe.get(), buf.get(), data.size() + 1);

    readFds.push_back(std::move(readPipe));

    writeFds.push_back(std::move(writePipe));
    writeBufs.push_back(std::move(buf));
    writeFutures.push_back(std::move(writeFuture));

  }

  // Now do async write
  int i = 0;
  for (const auto& fd : readFds) {
    // Try reading. This might not block anyway because there is a reader
    // on the other side.
    std::cout << "Attempting a async read on pipe " << i << std::endl;

    std::unique_ptr<char[]> buf(new char[bufsize]());
    std::future<SSIZE_T> readFuture = async::readAsync(fd.get(), buf.get(), bufsize);
    i++;

    readBufs.push_back(std::move(buf));
    readFutures.push_back(std::move(readFuture));
  }

  // Now, wait on the futures
  for (i = 0; i < max; i++) {
    std::future<SSIZE_T>& wFuture = writeFutures[i];
    wFuture.wait();
    SSIZE_T wBytes = wFuture.get();
    if (wBytes == -1) {
      std::cout << "Async Write FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::future<SSIZE_T>& rFuture = readFutures[i];
    rFuture.wait();
    SSIZE_T rBytes = rFuture.get();
    if (rBytes == -1) {
      std::cout << "Async Read FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::cout << "Pipe " << i << " write -> read as completed. Now checking results" << std::endl;
    if (rBytes != wBytes) {
      std::cout << "Pipe " << i << " does not match in read / write bytes = " << rBytes << " / " << wBytes << std::endl;
      throw;
    }

    bool failed = false;
    for (int j = 0; j < bufsize; j++) {
      if (readBufs[i][j] != writeBufs[i][j]) {
        failed = true;
        std::cout << "Pipe " << i << " differs on write buf on index " << j << ". "
          << "Read = " << readBufs[i][j] << " vs Write = " << writeBufs[i][j] << std::endl;
      }
    }

    if (failed) {
      throw;
    }

    std::cout << "Pipe " << i << " passed." << std::endl;
    std::cout << readBufs[i].get() << std::endl;
  }
}

void test_mix_pipe_test()
{
  auto deleter = [](async::Handle* h) {
    h->close();
    delete h;
  };

  std::vector<std::unique_ptr<async::Handle, decltype(deleter)>> readFds;
  std::vector<std::unique_ptr<char[]>> readBufs;
  std::vector<std::future<SSIZE_T>> readFutures;

  std::vector<std::unique_ptr<async::Handle, decltype(deleter)>> writeFds;
  std::vector<std::unique_ptr<char[]>> writeBufs;
  std::vector<std::future<SSIZE_T>> writeFutures;

  int max = 10;
  size_t bufsize = 1024;
  for (int i = 0; i < max; i++)
  {
    std::array<io::Handle*, 2> pipes = io::pipe(FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED);
    if (pipes[0] == nullptr || pipes[1] == nullptr) {
      std::cout << "FAILED creating pipes: " << ::GetLastError() << std::endl;
      throw;
    }

    // Make async handles
    std::unique_ptr<async::Handle, decltype(deleter)> readPipe(
      async::createAsyncHandle(pipes[0]),
      deleter);

    std::unique_ptr<async::Handle, decltype(deleter)> writePipe(
      async::createAsyncHandle(pipes[1]),
      deleter);

    pipes[0]->close();
    pipes[1]->close();
    delete pipes[0];
    delete pipes[1];

    // Try reading. It shouldn't block
    std::cout << "Attempting a async read on pipe " << i << std::endl;
    std::unique_ptr<char[]> buf(new char[bufsize]());
    std::future<SSIZE_T> readFuture = async::readAsync(readPipe.get(), buf.get(), bufsize);

    readFds.push_back(std::move(readPipe));
    readBufs.push_back(std::move(buf));
    readFutures.push_back(std::move(readFuture));
    writeFds.push_back(std::move(writePipe));

    timer_func(i, max);
  }

  // Now do async write
  int i = 0;
  for (const auto& fd : writeFds) {
    // Try writing. This might not block anyway because there is a reader
    // on the other side.
    std::cout << "Attempting a async write on pipe " << i << std::endl;

    std::string data = std::string("Hello World! From call ") + std::to_string(i);
    std::unique_ptr<char[]> buf(new char[bufsize]());
    memcpy(buf.get(), data.c_str(), data.size() + 1);

    std::future<SSIZE_T> writeFuture = async::writeAsync(fd.get(), buf.get(), data.size() + 1);
    i++;

    writeBufs.push_back(std::move(buf));
    writeFutures.push_back(std::move(writeFuture));
  }

  // Now, wait on the futures
  for (i = 0; i < max; i++) {
    std::future<SSIZE_T>& wFuture = writeFutures[i];
    wFuture.wait();
    SSIZE_T wBytes = wFuture.get();
    if (wBytes == -1) {
      std::cout << "Async Write FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::future<SSIZE_T>& rFuture = readFutures[i];
    rFuture.wait();
    SSIZE_T rBytes = rFuture.get();
    if (rBytes == -1) {
      std::cout << "Async Read FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::cout << "Pipe " << i << " write -> read as completed. Now checking results" << std::endl;
    if (rBytes != wBytes) {
      std::cout << "Pipe " << i << " does not match in read / write bytes = " << rBytes << " / " << wBytes << std::endl;
      throw;
    }

    bool failed = false;
    for (int j = 0; j < bufsize; j++) {
      if (readBufs[i][j] != writeBufs[i][j]) {
        failed = true;
        std::cout << "Pipe " << i << " differs on write buf on index " << j << ". "
          << "Read = " << readBufs[i][j] << " vs Write = " << writeBufs[i][j] << std::endl;
      }
    }

    if (failed) {
      throw;
    }

    std::cout << "Pipe " << i << " passed." << std::endl;
    std::cout << readBufs[i].get() << std::endl;
  }

  // sleep to wait for timers
  Sleep(10000);
}

void test_pipe_long()
{
  constexpr int buflen = 1024;
  constexpr int numBufs = 128;

  // Read 128K from file
  std::wstring fileName = L"";
  io::Handle* file = io::open(fileName.c_str(), GENERIC_READ, OPEN_EXISTING);
  if (file == nullptr) {
    std::cout << L"IO OPEN FAILED: " << GetLastError() << std::endl;
    throw;
  }

  unsigned char data[buflen * numBufs] = { 0 };
  SSIZE_T size = io::read(file, data, sizeof(data));
  if (size == -1) {
    std::cout << L"IO READ FAILED: " << GetLastError() << std::endl;
    file->close();
    delete file;
    throw;
  }

  file->close();
  delete file;

  std::array<io::Handle*, 2> pipes = io::pipe(FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED);
  if (pipes[0] == nullptr || pipes[1] == nullptr) {
    std::cout << "FAILED creating pipes: " << ::GetLastError() << std::endl;
    throw;
  }

  auto deleter = [](async::Handle* h) {
    h->close();
    delete h;
  };

  // Make async handles
  std::unique_ptr<async::Handle, decltype(deleter)> readPipe(
    async::createAsyncHandle(pipes[0]),
    deleter);

  std::unique_ptr<async::Handle, decltype(deleter)> writePipe(
    async::createAsyncHandle(pipes[1]),
    deleter);

  pipes[0]->close();
  pipes[1]->close();
  delete pipes[0];
  delete pipes[1];


  struct asyncData {
    std::future<SSIZE_T> future;
    unsigned char buf[buflen];
  };

  asyncData reads[numBufs];
  std::future<SSIZE_T> writes[numBufs];

  for (int i = 0; i < numBufs; i++) {
    std::cout << "Attempting a async read " << i << std::endl;
    reads[i].future = async::readAsync(readPipe.get(), reads[i].buf, buflen);
  }

  for (int i = 0; i < numBufs; i++) {
    std::cout << "Attempting a async write " << i << std::endl;
    writes[i] = async::writeAsync(writePipe.get(), data + i * buflen, buflen);
  }

  unsigned char data_copy[numBufs * buflen] = { 0 };
  for (int i = 0; i < numBufs; i++) {
    std::future<SSIZE_T>& wFuture = writes[i];
    wFuture.wait();
    SSIZE_T wBytes = wFuture.get();
    if (wBytes == -1) {
      std::cout << "Async Write FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::future<SSIZE_T>& rFuture = reads[i].future;
    rFuture.wait();
    SSIZE_T rBytes = rFuture.get();
    if (rBytes == -1) {
      std::cout << "Async Read FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::cout << "Pipe " << i << " write -> read as completed. Now checking results" << std::endl;
    if (rBytes != wBytes) {
      std::cout << "Pipe " << i << " does not match in read / write bytes = " << rBytes << " / " << wBytes << std::endl;
      throw;
    }

    memcpy(data_copy + i * buflen, reads[i].buf, buflen);
  }

  std::cout << "checking data" << std::endl;

  if (memcmp(data, data_copy, sizeof(data)) != 0) {
    std::cout << "Pipes didn't match in data!" << std::endl;
    throw;
  }
}

void test_pipe_long_alternate()
{
  constexpr int buflen = 1024;
  constexpr int numBufs = 128;

  // Read 128K from file
  std::wstring fileName = L"";
  io::Handle* file = io::open(fileName.c_str(), GENERIC_READ, OPEN_EXISTING);
  if (file == nullptr) {
    std::cout << L"IO OPEN FAILED: " << GetLastError() << std::endl;
    throw;
  }

  unsigned char data[buflen * numBufs] = { 0 };
  SSIZE_T size = io::read(file, data, sizeof(data));
  if (size == -1) {
    std::cout << L"IO READ FAILED: " << GetLastError() << std::endl;
    file->close();
    delete file;
    throw;
  }

  file->close();
  delete file;

  std::array<io::Handle*, 2> pipes = io::pipe(FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED);
  if (pipes[0] == nullptr || pipes[1] == nullptr) {
    std::cout << "FAILED creating pipes: " << ::GetLastError() << std::endl;
    throw;
  }

  auto deleter = [](async::Handle* h) {
    h->close();
    delete h;
  };

  // Make async handles
  std::unique_ptr<async::Handle, decltype(deleter)> readPipe(
    async::createAsyncHandle(pipes[0]),
    deleter);

  std::unique_ptr<async::Handle, decltype(deleter)> writePipe(
    async::createAsyncHandle(pipes[1]),
    deleter);

  pipes[0]->close();
  pipes[1]->close();
  delete pipes[0];
  delete pipes[1];

  struct asyncData {
    std::future<SSIZE_T> future;
    unsigned char buf[buflen];
  };

  asyncData reads[numBufs];
  std::future<SSIZE_T> writes[numBufs];

  for (int i = 0; i < numBufs; i++) {
    std::cout << "Attempting a async read " << i << std::endl;
    reads[i].future = async::readAsync(readPipe.get(), reads[i].buf, buflen);

    std::cout << "Attempting a async write " << i << std::endl;
    writes[i] = async::writeAsync(writePipe.get(), data + i * buflen, buflen);
  }

  unsigned char data_copy[numBufs * buflen] = { 0 };
  for (int i = 0; i < numBufs; i++) {
    std::future<SSIZE_T>& wFuture = writes[i];
    wFuture.wait();
    SSIZE_T wBytes = wFuture.get();
    if (wBytes == -1) {
      std::cout << "Async Write FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::future<SSIZE_T>& rFuture = reads[i].future;
    rFuture.wait();
    SSIZE_T rBytes = rFuture.get();
    if (rBytes == -1) {
      std::cout << "Async Read FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::cout << "Pipe " << i << " write -> read as completed. Now checking results" << std::endl;
    if (rBytes != wBytes) {
      std::cout << "Pipe " << i << " does not match in read / write bytes = " << rBytes << " / " << wBytes << std::endl;
      throw;
    }

    memcpy(data_copy + i * buflen, reads[i].buf, buflen);
  }

  std::cout << "checking data" << std::endl;

  if (memcmp(data, data_copy, sizeof(data)) != 0) {
    std::cout << "Pipes didn't match in data!" << std::endl;
    throw;
  }
}

BOOL CreateChildProcess(io::Handle* child_read, io::Handle* child_write)
// Create a child process that uses the previously created pipes for STDIN and STDOUT.
{
  wchar_t szCmdline[] = L"";
  PROCESS_INFORMATION piProcInfo;
  STARTUPINFO siStartInfo;
  BOOL bSuccess = FALSE;

  // Set up members of the PROCESS_INFORMATION structure. 

  ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

  // Set up members of the STARTUPINFO structure. 
  // This structure specifies the STDIN and STDOUT handles for redirection.
  SetHandleInformation(child_write->get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  SetHandleInformation(child_read->get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
  siStartInfo.cb = sizeof(STARTUPINFO);
  siStartInfo.hStdInput = (HANDLE)child_read->get();
  siStartInfo.hStdError = (HANDLE)child_write->get();
  siStartInfo.hStdOutput = (HANDLE)child_write->get();
  siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

  // Create the child process. 

  bSuccess = CreateProcess(NULL,
    szCmdline,     // command line 
    NULL,          // process security attributes 
    NULL,          // primary thread security attributes 
    TRUE,          // handles are inherited 
    0,             // creation flags 
    NULL,          // use parent's environment 
    NULL,          // use parent's current directory 
    &siStartInfo,  // STARTUPINFO pointer 
    &piProcInfo);  // receives PROCESS_INFORMATION 

                   // If an error occurs, exit the application. 
  if (!bSuccess)
    return FALSE;
  else
  {
    // Close handles to the child process and its primary thread.
    // Some applications might keep these handles to monitor the status
    // of the child process, for example. 
    child_read->close();
    delete child_read;
    child_write->close();
    delete child_write;
    CloseHandle(piProcInfo.hProcess);
    CloseHandle(piProcInfo.hThread);
    return TRUE;
  }
}


void test_child_process()
{
  auto deleter = [](async::Handle* h) {
    h->close();
    delete h;
  };

  std::array<io::Handle*, 2> pipes = io::pipe(FILE_FLAG_OVERLAPPED, 0);
  if (pipes[0] == nullptr || pipes[1] == nullptr) {
    std::cout << "FAILED creating pipes: " << ::GetLastError() << std::endl;
    throw;
  }

  // Make async handles
  std::unique_ptr<async::Handle, decltype(deleter)> readPipe_forparent(
    async::createAsyncHandle(pipes[0]),
    deleter);
  pipes[0]->close();
  delete pipes[0];

  std::array<io::Handle*, 2> pipes2 = io::pipe(0, FILE_FLAG_OVERLAPPED);
  if (pipes2[0] == nullptr || pipes2[1] == nullptr) {
    std::cout << "FAILED creating pipes: " << ::GetLastError() << std::endl;
    pipes[1]->close();
    delete pipes[1];
    throw;
  }

  // Make async handles
  std::unique_ptr<async::Handle, decltype(deleter)> writePipe_forparent(
    async::createAsyncHandle(pipes2[1]),
    deleter);
  pipes2[1]->close();
  delete pipes2[1];

  // Create the child process. 
  BOOL result = CreateChildProcess(pipes2[0], pipes[1]);
  if (!result)
  {
    std::cout << "FAILED creating process: " << ::GetLastError() << std::endl;
    pipes[1]->close();
    delete pipes[1];
    pipes2[0]->close();
    delete pipes2[0];
    throw;
  }

  char writebuf[] = "Hello World!";
  std::future<SSIZE_T> writeFuture = async::writeAsync(writePipe_forparent.get(), writebuf, sizeof(writebuf));

  char readbuf[1024] = { 0 };
  std::future<SSIZE_T> readFuture = async::readAsync(readPipe_forparent.get(), readbuf, sizeof(readbuf));

  writeFuture.wait();
  SSIZE_T wBytes = writeFuture.get();
  if (wBytes == -1) {
    std::cout << "Async Write FAILED: " << GetLastError() << std::endl;
    throw;
  }

  readFuture.wait();
  SSIZE_T rBytes = readFuture.get();
  if (rBytes == -1) {
    std::cout << "Async Read FAILED: " << GetLastError() << std::endl;
    throw;
  }

  printf("data: %s\n", readbuf);
}

void test_socket()
{
  int iResult;
  IN_ADDR addr;
  if (InetPton(AF_INET, L"127.0.0.1", &addr) != 1) {
    printf("Error at creating addr: %ld\n", WSAGetLastError());
    return;
  }
  sockaddr_in sock_addr = { 0 };
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(DEFAULT_PORT);
  sock_addr.sin_addr = addr;

  // Create listening socket
  std::cout << "Creating listening socket on localhost:27015" << std::endl;
  SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (ListenSocket == INVALID_SOCKET) {
    printf("Error at listen socket(): %ld\n", WSAGetLastError());
    return;
  }

  iResult = bind(ListenSocket, (sockaddr*) &sock_addr, (int) sizeof(sock_addr));
  if (iResult != 0) {
    printf("Error at listen bind(): %ld\n", WSAGetLastError());
    closesocket(ListenSocket);
    return;
  }

  std::cout << "Associating listen socket with IOCP..." << std::endl;
  async::SocketHandle listen_h(ListenSocket);

  std::cout << "Listening on socket now..." << std::endl;
  iResult = listen_h.listen(1);
  if (iResult != 0) {
    printf("Error at listen listen(): %ld\n", WSAGetLastError());
    listen_h.close();
    return;
  }

  // Normally this will block
  std::cout << "Now accepting connections on socket now..." << std::endl;
  std::future<async::SocketHandle*> acceptFuture = listen_h.accept();

  // Try connecting.
  std::cout << std::endl << "Creating connect (client) socket" << std::endl;
  SOCKET ConnectSocketFake = socket(AF_INET, SOCK_STREAM, 0);
  if (ConnectSocketFake == INVALID_SOCKET) {
    printf("Error at connect socket(): %ld\n", WSAGetLastError());
    listen_h.close();
    return;
  }

  // Connect to a unroutable address. This should hang and time out async.
  std::cout << "Connecting to unroutable address (10.0.0.0:27015)" << std::endl;
  IN_ADDR addr_fake;
  if (InetPton(AF_INET, L"10.0.0.0", &addr_fake) != 1) {
    printf("Error at creating addr: %ld\n", WSAGetLastError());
    listen_h.close();
    closesocket(ConnectSocketFake);
    return;
  }
  sockaddr_in sock_addr_fake = { 0 };
  sock_addr_fake.sin_family = AF_INET;
  sock_addr_fake.sin_port = htons(DEFAULT_PORT);
  sock_addr_fake.sin_addr = addr_fake;

  async::SocketHandle connect_fake_h(ConnectSocketFake);
  std::future<DWORD> connectFakeFuture = connect_fake_h.connect((sockaddr*)&sock_addr_fake, (int)sizeof(sock_addr_fake));

  // Create socket for the regular client connection.
  std::cout << std::endl << "Creating connect (client) socket" << std::endl;
  SOCKET ConnectSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (ConnectSocket == INVALID_SOCKET) {
    printf("Error at connect socket(): %ld\n", WSAGetLastError());
    listen_h.close();
    connect_fake_h.close();
    return;
  }

  std::cout << "Connecting to listening socket at localhost:27015" << std::endl;
  async::SocketHandle connect_h(ConnectSocket);
  std::future<DWORD> connectFuture = connect_h.connect((sockaddr*) &sock_addr, (int)sizeof(sock_addr));

  acceptFuture.wait();
  async::SocketHandle* accepted_h = acceptFuture.get();
  connectFuture.wait();

  // Now we have a client server connection.
  std::cout << "client <-> server connected!" << std::endl;

  // client recv and server send
  std::cout << std::endl << "client reading 'Hello World!' from server" << std::endl;
  char buf[1024] = { 0 };
  std::future<SSIZE_T> readFuture = connect_h.readAsync(buf, sizeof(buf));
  char writeBuf[] = "Hello World!";
  std::future<SSIZE_T> writeFuture = accepted_h->writeAsync(writeBuf, sizeof(writeBuf));
  printf("client received: %s\n", buf);

  // client sendfile and server recv
  std::cout << std::endl << "client writing macbeth.txt to server" << std::endl;
  char sendfilebuf[64 * 1024] = { 0 };
  std::future<SSIZE_T> readSendFileFuture = accepted_h->readAsync(sendfilebuf, sizeof(sendfilebuf));

  HANDLE fileHandle = CreateFile(
    L"",
    GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL);
  if (fileHandle == INVALID_HANDLE_VALUE) {
    printf("Error at sendfile::CreateFile(): %ld\n", GetLastError());
    connect_fake_h.close();
    connect_h.close();
    accepted_h->close();
    delete accepted_h;
    listen_h.close();
    return;
  }

  LARGE_INTEGER fileSize;
  if (GetFileSizeEx(fileHandle, &fileSize) == FALSE) {
    printf("Error at sendfile::GetFileSizeEx(): %ld\n", GetLastError());
    connect_fake_h.close();
    connect_h.close();
    accepted_h->close();
    delete accepted_h;
    listen_h.close();
    CloseHandle(fileHandle);
    return;
  }

  io::FileHandle ioFileHandle(fileHandle);
  std::future<SSIZE_T> sendFileFuture = connect_h.sendfile(&ioFileHandle, 0, (size_t)fileSize.QuadPart);
  
  sendFileFuture.wait();
  readSendFileFuture.wait();
  ioFileHandle.close();

  printf("server received: %s\n", sendfilebuf);

  // wait for the connect on invalid address
  std::cout << std::endl << "waiting for invalid connect to fail" << std::endl;
  connectFakeFuture.wait();
 
  listen_h.close();
  connect_h.close();
  connect_fake_h.close();
  accepted_h->close();
  delete accepted_h;
}

void test_dup_socket()
{
  int iResult;
  IN_ADDR addr;
  if (InetPton(AF_INET, L"127.0.0.1", &addr) != 1) {
    printf("Error at creating addr: %ld\n", WSAGetLastError());
    return;
  }
  sockaddr_in sock_addr = { 0 };
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(DEFAULT_PORT);
  sock_addr.sin_addr = addr;

  // Create listening socket
  std::cout << "Creating listening socket on localhost:27015" << std::endl;
  SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (ListenSocket == INVALID_SOCKET) {
    printf("Error at listen socket(): %ld\n", WSAGetLastError());
    return;
  }

  iResult = bind(ListenSocket, (sockaddr*)&sock_addr, (int) sizeof(sock_addr));
  if (iResult != 0) {
    printf("Error at listen bind(): %ld\n", WSAGetLastError());
    closesocket(ListenSocket);
    return;
  }
  
  auto deleter = [](async::SocketHandle* handle) {
    handle->close();
    delete handle;
  };

  std::cout << "Associating listen socket with IOCP..." << std::endl;
  io::SocketHandle listen_io_h(ListenSocket);
  std::unique_ptr<async::SocketHandle, decltype(deleter)> listen_h(
    dynamic_cast<async::SocketHandle*>(async::createAsyncHandle(&listen_io_h)),
    deleter);
  listen_io_h.close();
  
  std::cout << "Listening on socket now..." << std::endl;
  iResult = listen_h->listen(1);
  if (iResult != 0) {
    printf("Error at listen listen(): %ld\n", WSAGetLastError());
    return;
  }

  // Normally this will block
  std::cout << "Now accepting connections on socket now..." << std::endl;
  std::future<async::SocketHandle*> acceptFuture = listen_h->accept();

  // Create socket for the regular client connection.
  std::cout << std::endl << "Creating connect (client) socket" << std::endl;
  SOCKET ConnectSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (ConnectSocket == INVALID_SOCKET) {
    printf("Error at connect socket(): %ld\n", WSAGetLastError());
    return;
  }

  std::cout << "Connecting to listening socket at localhost:27015" << std::endl;
  io::SocketHandle connect_io_h(ConnectSocket);
  std::unique_ptr<async::SocketHandle, decltype(deleter)> connect_h(
    dynamic_cast<async::SocketHandle*>(async::createAsyncHandle(&connect_io_h)),
    deleter);
  connect_io_h.close();

  std::future<DWORD> connectFuture = connect_h->connect((sockaddr*)&sock_addr, (int)sizeof(sock_addr));

  acceptFuture.wait();
  std::unique_ptr<async::SocketHandle, decltype(deleter)> accepted_h(
    acceptFuture.get(),
    deleter); 
  connectFuture.wait();

  // Now we have a client server connection.
  std::cout << "client <-> server connected!" << std::endl;

  // client recv and server send
  std::cout << std::endl << "client reading 'Hello World!' from server" << std::endl;
  char buf[1024] = { 0 };
  std::future<SSIZE_T> readFuture = connect_h->readAsync(buf, sizeof(buf));
  char writeBuf[] = "Hello World!";
  std::future<SSIZE_T> writeFuture = accepted_h->writeAsync(writeBuf, sizeof(writeBuf));
  printf("client received: %s\n", buf);

  // client sendfile and server recv
  std::cout << std::endl << "client writing macbeth.txt to server" << std::endl;
  char sendfilebuf[64 * 1024] = { 0 };
  std::future<SSIZE_T> readSendFileFuture = accepted_h->readAsync(sendfilebuf, sizeof(sendfilebuf));

  HANDLE fileHandle = CreateFile(
    L"",
    GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL);
  if (fileHandle == INVALID_HANDLE_VALUE) {
    printf("Error at sendfile::CreateFile(): %ld\n", GetLastError());
    return;
  }

  LARGE_INTEGER fileSize;
  if (GetFileSizeEx(fileHandle, &fileSize) == FALSE) {
    printf("Error at sendfile::GetFileSizeEx(): %ld\n", GetLastError());
    CloseHandle(fileHandle);
    return;
  }

  io::FileHandle ioFileHandle(fileHandle);
  std::future<SSIZE_T> sendFileFuture = connect_h->sendfile(&ioFileHandle, 0, (size_t)fileSize.QuadPart);

  sendFileFuture.wait();
  readSendFileFuture.wait();
  ioFileHandle.close();

  printf("server received: %s\n", sendfilebuf);
}

void test_all()
{
  auto deleter = [](async::Handle* h) {
    h->close();
    delete h;
  };

  int iResult;
  IN_ADDR addr;
  if (InetPton(AF_INET, L"127.0.0.1", &addr) != 1) {
    printf("Error at creating addr: %ld\n", WSAGetLastError());
    return;
  }
  sockaddr_in sock_addr = { 0 };
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(DEFAULT_PORT);
  sock_addr.sin_addr = addr;

  // Create listening socket
  std::cout << "Creating listening socket on localhost:27015" << std::endl;
  SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (ListenSocket == INVALID_SOCKET) {
    printf("Error at listen socket(): %ld\n", WSAGetLastError());
    return;
  }

  iResult = bind(ListenSocket, (sockaddr*)&sock_addr, (int) sizeof(sock_addr));
  if (iResult != 0) {
    printf("Error at listen bind(): %ld\n", WSAGetLastError());
    closesocket(ListenSocket);
    return;
  }

  std::cout << "Associating listen socket with IOCP..." << std::endl;
  io::SocketHandle listen_io_h(ListenSocket);
  std::unique_ptr<async::SocketHandle, decltype(deleter)> listen_h(
    dynamic_cast<async::SocketHandle*>(async::createAsyncHandle(&listen_io_h)),
    deleter);
  listen_io_h.close();

  std::cout << "Listening on socket now..." << std::endl;
  iResult = listen_h->listen(1);
  if (iResult != 0) {
    printf("Error at listen listen(): %ld\n", WSAGetLastError());
    return;
  }

  int max = 10;
  size_t bufsize = 1024;
  std::vector<std::unique_ptr<async::Handle, decltype(deleter)>> readFds;
  std::vector<std::unique_ptr<char[]>> readBufs;
  std::vector<std::future<SSIZE_T>> readFutures;
  std::vector<std::future<async::SocketHandle*>> acceptFutures;

  std::vector<std::unique_ptr<async::Handle, decltype(deleter)>> writeFds;
  std::vector<std::unique_ptr<char[]>> writeBufs;
  std::vector<std::future<SSIZE_T>> writeFutures;
  std::vector<std::unique_ptr<async::SocketHandle, decltype(deleter)>> connectSockets;
  std::vector<std::future<DWORD>> connectFutures;
  for (int i = 0; i < max; i++)
  {
    std::array<io::Handle*, 2> pipes = io::pipe(FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED);
    if (pipes[0] == nullptr || pipes[1] == nullptr) {
      std::cout << "FAILED creating pipes: " << ::GetLastError() << std::endl;
      throw;
    }

    // Make async handles
    std::unique_ptr<async::Handle, decltype(deleter)> readPipe(
      async::createAsyncHandle(pipes[0]),
      deleter);

    std::unique_ptr<async::Handle, decltype(deleter)> writePipe(
      async::createAsyncHandle(pipes[1]),
      deleter);

pipes[0]->close();
pipes[1]->close();
delete pipes[0];
delete pipes[1];

// Try reading. It shouldn't block
std::cout << "Attempting a async read on pipe " << i << std::endl;
std::unique_ptr<char[]> buf(new char[bufsize]());
std::future<SSIZE_T> readFuture = async::readAsync(readPipe.get(), buf.get(), bufsize);

readFds.push_back(std::move(readPipe));
readBufs.push_back(std::move(buf));
readFutures.push_back(std::move(readFuture));
writeFds.push_back(std::move(writePipe));

// timer
timer_func(i, max);

// accept futures
std::cout << "Now accepting connections on socket" << std::endl;
acceptFutures.push_back(std::move(listen_h->accept()));
  }

  // Now do async write
  int i = 0;
  for (const auto& fd : writeFds) {
    // Try writing. This might not block anyway because there is a reader
    // on the other side.
    std::cout << "Attempting a async write on pipe " << i << std::endl;

    std::string data = std::string("Hello World! From call ") + std::to_string(i);
    std::unique_ptr<char[]> buf(new char[bufsize]());
    memcpy(buf.get(), data.c_str(), data.size() + 1);

    std::future<SSIZE_T> writeFuture = async::writeAsync(fd.get(), buf.get(), data.size() + 1);
    i++;

    writeBufs.push_back(std::move(buf));
    writeFutures.push_back(std::move(writeFuture));

    // connect futures
    // Create socket for the regular client connection.
    std::cout << std::endl << "Creating connect (client) socket" << std::endl;
    SOCKET ConnectSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (ConnectSocket == INVALID_SOCKET) {
      printf("Error at connect socket(): %ld\n", WSAGetLastError());
      return;
    }

    std::cout << "Connecting to listening socket at localhost:27015" << std::endl;
    io::SocketHandle connect_io_h(ConnectSocket);
    std::unique_ptr<async::SocketHandle, decltype(deleter)> connect_h(
      dynamic_cast<async::SocketHandle*>(async::createAsyncHandle(&connect_io_h)),
      deleter);
    connect_io_h.close();

    connectFutures.push_back(connect_h->connect((sockaddr*)&sock_addr, (int)sizeof(sock_addr)));
    connectSockets.push_back(std::move(connect_h));
  }

  // Now, wait on the futures
  std::vector<std::unique_ptr<char[]>> readBufs1;
  std::vector<std::future<SSIZE_T>> readFutures1;
  std::vector<std::future<SSIZE_T>> writeFutures1;

  std::vector<std::unique_ptr<char[]>> readBufs2;
  std::vector<std::future<SSIZE_T>> readFutures2;
  std::vector<HANDLE> writeHandles;
  std::vector<std::future<SSIZE_T>> writeFutures2;

  std::vector<std::unique_ptr<async::SocketHandle, decltype(deleter)>> acceptedSockets;
  const char writeBuf1[] = "Hello World!";
  for (i = 0; i < max; i++) {
    std::future<SSIZE_T>& wFuture = writeFutures[i];
    wFuture.wait();
    SSIZE_T wBytes = wFuture.get();
    if (wBytes == -1) {
      std::cout << "Async Write FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::future<SSIZE_T>& rFuture = readFutures[i];
    rFuture.wait();
    SSIZE_T rBytes = rFuture.get();
    if (rBytes == -1) {
      std::cout << "Async Read FAILED: " << GetLastError() << std::endl;
      throw;
    }

    std::cout << "Pipe " << i << " write -> read as completed. Now checking results" << std::endl;
    if (rBytes != wBytes) {
      std::cout << "Pipe " << i << " does not match in read / write bytes = " << rBytes << " / " << wBytes << std::endl;
      throw;
    }

    bool failed = false;
    for (int j = 0; j < bufsize; j++) {
      if (readBufs[i][j] != writeBufs[i][j]) {
        failed = true;
        std::cout << "Pipe " << i << " differs on write buf on index " << j << ". "
          << "Read = " << readBufs[i][j] << " vs Write = " << writeBufs[i][j] << std::endl;
      }
    }

    std::cout << "Pipe " << i << " passed." << std::endl;
    std::cout << readBufs[i].get() << std::endl;

    std::cout << std::endl << "Accepting client connection" << std::endl;
    acceptFutures[i].wait();
    connectFutures[i].wait();

    if (connectFutures[i].get() != 0) {
      failed = true;
    }
    
    async::SocketHandle* accepted_h = acceptFutures[i].get();
    if (accepted_h == nullptr) {
      failed = true;
    } else {
      // client recv and server send
      std::cout << std::endl << "client reading 'Hello World!' from server" << std::endl;
      std::unique_ptr<char[]> readbuf(new char[bufsize]());
      std::future<SSIZE_T> readFuture = connectSockets[i]->readAsync(readbuf.get(), bufsize);
      std::future<SSIZE_T> writeFuture = accepted_h->writeAsync(writeBuf1, sizeof(writeBuf1));
      readBufs1.push_back(std::move(readbuf));
      readFutures1.push_back(std::move(readFuture));
      writeFutures1.push_back(std::move(writeFuture));
      
      // client sendfile and server recv
      std::cout << std::endl << "client writing helloworld.txt to server" << std::endl;
      LARGE_INTEGER fileSize;
      HANDLE fileHandle = CreateFile(
        L"",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
      if (fileHandle == INVALID_HANDLE_VALUE) {
        printf("Error at sendfile::CreateFile(): %ld\n", GetLastError());
        failed = true;
        goto end;
      }

      if (GetFileSizeEx(fileHandle, &fileSize) == FALSE) {
        printf("Error at sendfile::GetFileSizeEx(): %ld\n", GetLastError());
        CloseHandle(fileHandle);
        failed = true;
        goto end;
      }

      std::unique_ptr<char[]> readSendBuf(new char[fileSize.QuadPart+1]());
      readSendBuf[fileSize.QuadPart] = '\0';
      std::future<SSIZE_T> readSendFuture = accepted_h->readAsync(readSendBuf.get(), fileSize.QuadPart);
      io::FileHandle ioFileHandle(fileHandle);
      std::future<SSIZE_T> sendFileFuture = connectSockets[i]->sendfile(&ioFileHandle, 0, (size_t)fileSize.QuadPart);

      readBufs2.push_back(std::move(readSendBuf));
      readFutures2.push_back(std::move(readSendFuture));
      writeFutures2.push_back(std::move(sendFileFuture));
      writeHandles.push_back(fileHandle);
    }

end:
    if (accepted_h != nullptr) {
      acceptedSockets.push_back(std::unique_ptr<async::SocketHandle, decltype(deleter)>(accepted_h, deleter));
    }

    if (failed) {
      for (HANDLE& h : writeHandles) { CloseHandle(h); }
      throw;
    }
  }

  // Now, we wait for all the remaining futures.
  for (i = 0; i < max; i++) {
    writeFutures1[i].wait();
    writeFutures2[i].wait();
    readFutures1[i].wait();
    readFutures2[i].wait();

    SSIZE_T rf1 = readFutures1[i].get();
    SSIZE_T rf2 = readFutures2[i].get();
    SSIZE_T wf1 = writeFutures1[i].get();
    SSIZE_T wf2 = writeFutures2[i].get();
    if (rf1 == -1 || rf2 == -1 || wf1 == -1 || wf2 == -1) {
      std::cout << " Server <-> client write/write failed." << std::endl;
      for (HANDLE& h : writeHandles) { CloseHandle(h); }
      throw;
    }

    std::cout << "Read / Writes " << rf1 << " " << rf2 << " " << wf1 << " " << wf2 << std::endl;
    printf("Client got for first read: %s\n", readBufs1[i].get());
    printf("Client got for second read: %s\n\n", readBufs2[i].get());
  }

  for (HANDLE& h : writeHandles) { CloseHandle(h); }

  // sleep to wait for timers
  Sleep(10000);
}

struct A {
  int x;
  int y;
};

template <typename T, typename U> 
struct B {

};

int main()
{
  if (init_wsa() != 0) {
    return 1;
  }

  loop::EventLoop::initialize();
  
  // Create event loop
  std::thread* eventloop = new std::thread(&loop::EventLoop::run);

  /*test_pipes();
  test_pipe_write_first();
  test_files();
  test_timers();
  test_mix_pipe_test();
  test_pipe_long_alternate();
  test_child_process();
  test_socket();*/
  // test_dup_socket();
  test_all();

  loop::EventLoop::stop();

  eventloop->join();
  delete eventloop;

  close_wsa();
  return 0;
}

