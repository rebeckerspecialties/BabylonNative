// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "child_process.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

constexpr size_t ChunkSize = 8192;
constexpr size_t ChunkCount = 128;

std::string Chunk(size_t index, char first) {
  return std::string(ChunkSize, static_cast<char>(first + index % 26));
}

bool Write(FILE* stream, const std::string& text) {
  return std::fwrite(text.data(), 1, text.size(), stream) == text.size() &&
         std::fflush(stream) == 0;
}

int RunChild(const std::string& mode) {
  if (mode == "empty") return 0;
  if (mode == "nonzero") {
    return Write(stdout, "output before failure\n") &&
                   Write(stderr, "error before failure\n")
               ? 23
               : 1;
  }
  if (mode != "interleaved") return 2;
  for (size_t i = 0; i < ChunkCount; ++i) {
    if (!Write(stdout, Chunk(i, 'A')) || !Write(stderr, Chunk(i, 'a'))) {
      return 1;
    }
  }
  return 0;
}

// A regression must fail in bounded time rather than hang the test runner.
class Deadline {
 public:
  Deadline() : thread_([this] {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!changed_.wait_for(lock, std::chrono::seconds(10), [this] { return done_; })) {
      std::fprintf(stderr, "FAIL: SpawnSync exceeded the 10-second deadline\n");
      std::fflush(stderr);
      std::_Exit(124);
    }
  }) {}

  ~Deadline() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      done_ = true;
    }
    changed_.notify_one();
    thread_.join();
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool done_ = false;
  std::thread thread_;
};

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void RunTest(const std::string& executable, const std::string& mode) {
  Deadline deadline;
  const auto result = node_api_tests::SpawnSync(executable, {"--child", mode});
  if (mode == "empty") {
    Require(result.status == 0, "empty child exit status");
    Require(result.std_output.empty(), "empty child stdout");
    Require(result.std_error.empty(), "empty child stderr");
  } else if (mode == "nonzero") {
    Require(result.status == 23, "nonzero child exit status");
    Require(result.std_output == "output before failure\n", "nonzero child stdout");
    Require(result.std_error == "error before failure\n", "nonzero child stderr");
  } else {
    Require(result.status == 0, "interleaved child exit status");
    std::string expectedOut;
    std::string expectedErr;
    for (size_t i = 0; i < ChunkCount; ++i) {
      expectedOut += Chunk(i, 'A');
      expectedErr += Chunk(i, 'a');
    }
    Require(result.std_output == expectedOut, "interleaved stdout bytes/order");
    Require(result.std_error == expectedErr, "interleaved stderr bytes/order");
  }
  std::printf("PASS %s: status=%u stdout=%zu stderr=%zu\n", mode.c_str(),
              result.status, result.std_output.size(), result.std_error.size());
  std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--child") {
    return RunChild(argv[2]);
  }
  if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--case")) return 2;
  const std::string selected = argc == 3 ? argv[2] : "all";
  if (selected != "all" && selected != "empty" && selected != "nonzero" &&
      selected != "interleaved") return 2;
  std::string executable = std::filesystem::absolute(argv[0]).string();
#if defined(_WIN32)
  executable = '"' + executable + '"';
#endif
  try {
    for (const char* mode : {"empty", "nonzero", "interleaved"}) {
      if (selected == "all" || selected == mode) RunTest(executable, mode);
    }
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
