// Mesos_nolibevent.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "eventloop.hpp"
#include "io.hpp"
#include "async_io.hpp"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Rpcrt4.lib")

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

  std::wstring writtenFile = file + L"_copy.txt";
  std::unique_ptr<io::Handle, decltype(deleter)> writeFile(
    io::open(writtenFile.c_str(), GENERIC_WRITE, CREATE_ALWAYS),
    deleter);

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
  std::unique_ptr<async::Handle, decltype(deleter)> hAsyncRead(
    async::createAsyncHandle(hRead),
    deleter);

  io::close(hRead);
  delete hRead;

  std::wstring writtenFile = file + L"_copy_async.txt";
  io::Handle* hWrite = io::open(writtenFile.c_str(), GENERIC_WRITE, CREATE_ALWAYS);
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

int main()
{
  loop::EventLoop::initialize();
  
  // Create event loop
  std::thread* eventloop = new std::thread(&loop::EventLoop::run);
  
  test_pipes();
  test_pipe_write_first();
  test_files();
  test_timers();
  test_mix_pipe_test();

  loop::EventLoop::stop();

  eventloop->join();
  delete eventloop;
  return 0;
}

