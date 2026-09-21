# poll_wrapper
poll / epoll 的极简 C++ 封装，尽量保持接口简短，同时用 RAII 处理资源释放。

## 提供内容

- `poll_wrapper::poll_wait`：对 `poll(2)` 的轻量封装，自动重试 `EINTR`，其他错误抛出 `std::system_error`
- `poll_wrapper::epoll`：对 `epoll_create1 / epoll_ctl / epoll_wait` 的 RAII 封装，等待时自动重试 `EINTR`

头文件位置：

```text
include/poll_wrapper.hpp
```

## 快速测试

```bash
g++ -std=c++17 -Wall -Wextra -Werror -pthread -Iinclude tests/poll_wrapper_test.cpp -o /tmp/poll_wrapper_test
/tmp/poll_wrapper_test
```
