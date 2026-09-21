#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

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

void expect_epoll_invalid_argument() {
  poll_wrapper::epoll ep;

  bool thrown = false;
  try {
    struct epoll_event event {};
    (void)ep.wait(&event, 0, 0);
  } catch (const std::invalid_argument&) {
    thrown = true;
  }

  assert(thrown);
}

}  // namespace

int main() {
  expect_poll_readable();
  expect_empty_poll_vector();
  expect_epoll_readable();
  expect_epoll_invalid_argument();
  return 0;
}
