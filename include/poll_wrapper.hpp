#pragma once

#include <cerrno>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/epoll.h>
#include <poll.h>
#include <unistd.h>

namespace poll_wrapper {

inline void throw_system_error(const char* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

inline int poll_wait(struct pollfd* fds, nfds_t count, int timeout_ms) {
  for (;;) {
    const int rc = ::poll(fds, count, timeout_ms);
    if (rc >= 0) {
      return rc;
    }
    if (errno != EINTR) {
      throw_system_error("poll");
    }
  }
}

inline int poll_wait(std::vector<struct pollfd>& fds, int timeout_ms) {
  return poll_wait(fds.empty() ? nullptr : fds.data(), fds.size(), timeout_ms);
}

class epoll {
 public:
  explicit epoll(int flags = EPOLL_CLOEXEC) : fd_(::epoll_create1(flags)) {
    if (fd_ < 0) {
      throw_system_error("epoll_create1");
    }
  }

  ~epoll() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  epoll(const epoll&) = delete;
  epoll& operator=(const epoll&) = delete;

  epoll(epoll&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

  epoll& operator=(epoll&& other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) {
        ::close(fd_);
      }
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  int fd() const noexcept { return fd_; }

  void add(int target_fd, std::uint32_t events) {
    ctl(EPOLL_CTL_ADD, target_fd, events);
  }

  void modify(int target_fd, std::uint32_t events) {
    ctl(EPOLL_CTL_MOD, target_fd, events);
  }

  void remove(int target_fd) {
    if (::epoll_ctl(fd_, EPOLL_CTL_DEL, target_fd, nullptr) < 0) {
      throw_system_error("epoll_ctl(DEL)");
    }
  }

  int wait(struct epoll_event* events, int max_events, int timeout_ms) {
    if (max_events <= 0) {
      throw std::invalid_argument("max_events must be positive");
    }
    if (events == nullptr) {
      throw std::invalid_argument("events must not be null");
    }
    for (;;) {
      const int rc = ::epoll_wait(fd_, events, max_events, timeout_ms);
      if (rc >= 0) {
        return rc;
      }
      if (errno != EINTR) {
        throw_system_error("epoll_wait");
      }
    }
  }

  std::vector<struct epoll_event> wait(int max_events, int timeout_ms) {
    if (max_events <= 0) {
      throw std::invalid_argument("max_events must be positive");
    }

    std::unique_ptr<struct epoll_event[]> events(new struct epoll_event[static_cast<std::size_t>(max_events)]);
    const int ready = wait(events.get(), max_events, timeout_ms);
    return std::vector<struct epoll_event>(events.get(), events.get() + ready);
  }

 private:
  void ctl(int op, int target_fd, std::uint32_t events) {
    struct epoll_event event {};
    event.events = events;
    event.data.fd = target_fd;
    if (::epoll_ctl(fd_, op, target_fd, &event) < 0) {
      throw_system_error("epoll_ctl");
    }
  }

  int fd_;
};

}  // namespace poll_wrapper
