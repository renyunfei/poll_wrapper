#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <csignal>
#include <stdexcept>
#include <thread>
#include <vector>

#include <pthread.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "poll_wrapper.hpp"

namespace {

class scoped_fd {
 public:
  explicit scoped_fd(int fd = -1) : fd_(fd) {}
  ~scoped_fd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  scoped_fd(const scoped_fd&) = delete;
  scoped_fd& operator=(const scoped_fd&) = delete;

  scoped_fd(scoped_fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

  scoped_fd& operator=(scoped_fd&& other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) {
        ::close(fd_);
      }
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  int get() const { return fd_; }

 private:
  int fd_;
};

class scoped_signal_handler {
 public:
  explicit scoped_signal_handler(int signal_number) : signal_number_(signal_number) {
    struct sigaction action {};
    action.sa_handler = &scoped_signal_handler::handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    assert(::sigaction(signal_number_, &action, &old_action_) == 0);
  }

  ~scoped_signal_handler() { assert(::sigaction(signal_number_, &old_action_, nullptr) == 0); }

  scoped_signal_handler(const scoped_signal_handler&) = delete;
  scoped_signal_handler& operator=(const scoped_signal_handler&) = delete;

 private:
  static void handle_signal(int) {}

  int signal_number_;
  struct sigaction old_action_ {};
};

void interrupt_then_write(int write_fd, pthread_t waiter_thread) {
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  assert(::pthread_kill(waiter_thread, SIGUSR1) == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const char payload = 'z';
  assert(::write(write_fd, &payload, sizeof(payload)) == 1);
}

void expect_poll_readable() {
  int pipe_fds[2];
  assert(::pipe(pipe_fds) == 0);
  scoped_fd read_fd(pipe_fds[0]);
  scoped_fd write_fd(pipe_fds[1]);

  struct pollfd pfd {};
  pfd.fd = read_fd.get();
  pfd.events = POLLIN;

  const char payload = 'x';
  assert(::write(write_fd.get(), &payload, sizeof(payload)) == 1);
  assert(poll_wrapper::poll_wait(&pfd, 1, 1000) == 1);
  assert((pfd.revents & POLLIN) != 0);
}

void expect_empty_poll_vector() {
  std::vector<struct pollfd> fds;
  assert(poll_wrapper::poll_wait(fds, 0) == 0);
}

void expect_epoll_readable() {
  int pipe_fds[2];
  assert(::pipe(pipe_fds) == 0);
  scoped_fd read_fd(pipe_fds[0]);
  scoped_fd write_fd(pipe_fds[1]);

  poll_wrapper::epoll ep;
  ep.add(read_fd.get(), EPOLLIN);

  const char payload = 'y';
  assert(::write(write_fd.get(), &payload, sizeof(payload)) == 1);

  auto events = ep.wait(1, 1000);
  assert(events.size() == 1);
  assert(events[0].data.fd == read_fd.get());
  assert((events[0].events & EPOLLIN) != 0);
}

void expect_interrupted_waits_retry() {
  scoped_signal_handler signal_handler(SIGUSR1);
  const pthread_t waiter_thread = ::pthread_self();

  {
    int pipe_fds[2];
    assert(::pipe(pipe_fds) == 0);
    scoped_fd read_fd(pipe_fds[0]);
    scoped_fd write_fd(pipe_fds[1]);

    struct pollfd pfd {};
    pfd.fd = read_fd.get();
    pfd.events = POLLIN;

    std::thread notifier(interrupt_then_write, write_fd.get(), waiter_thread);
    assert(poll_wrapper::poll_wait(&pfd, 1, 1000) == 1);
    notifier.join();
    assert((pfd.revents & POLLIN) != 0);
  }

  {
    int pipe_fds[2];
    assert(::pipe(pipe_fds) == 0);
    scoped_fd read_fd(pipe_fds[0]);
    scoped_fd write_fd(pipe_fds[1]);

    poll_wrapper::epoll ep;
    ep.add(read_fd.get(), EPOLLIN);

    std::thread notifier(interrupt_then_write, write_fd.get(), waiter_thread);
    auto events = ep.wait(1, 1000);
    notifier.join();
    assert(events.size() == 1);
    assert(events[0].data.fd == read_fd.get());
    assert((events[0].events & EPOLLIN) != 0);
  }
}

void expect_epoll_invalid_argument() {
  poll_wrapper::epoll ep;

  bool invalid_size_thrown = false;
  try {
    struct epoll_event event {};
    (void)ep.wait(&event, 0, 0);
  } catch (const std::invalid_argument&) {
    invalid_size_thrown = true;
  }

  bool null_events_thrown = false;
  try {
    (void)ep.wait(nullptr, 1, 0);
  } catch (const std::invalid_argument&) {
    null_events_thrown = true;
  }

  assert(invalid_size_thrown);
  assert(null_events_thrown);
}

}  // namespace

int main() {
  expect_poll_readable();
  expect_empty_poll_vector();
  expect_epoll_readable();
  expect_interrupted_waits_retry();
  expect_epoll_invalid_argument();
  return 0;
}
