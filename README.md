<h1 align="center">⚡ uring-kv</h1>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00FF41?style=flat-square">
  <img alt="CMake" src="https://img.shields.io/badge/build-CMake-00FF41?style=flat-square">
  <img alt="Linux 5.10+" src="https://img.shields.io/badge/Linux-5.10%2B-00FF41?style=flat-square">
  <a href="LICENSE"><img alt="MIT" src="https://img.shields.io/badge/license-MIT-00FF41?style=flat-square"></a>
  <img alt="platform: linux" src="https://img.shields.io/badge/platform-linux-00FF41?style=flat-square">
</p>

<p align="center"><em><code>Sub-millisecond async TCP key-value store — io_uring all the way down</code></em></p>

> 🇷🇺 [Читать на русском](README.ru.md)

---

## ⚡ What & Why

`io_uring` is Linux's completion-based async I/O interface: work is submitted through SQEs and completed through CQEs, avoiding the readiness-then-syscall pattern of `epoll`. For this server, `accept`, `recv`, and `send` all go through the ring, with no blocking syscalls in the hot path. The result is a compact event loop that keeps connection state, buffers, and protocol handling explicit. This project is written as a real backend systems portfolio piece, not a tutorial wrapper around a framework.

---

## 🚀 Quick Start

```bash
sudo apt install liburing-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
./build/uring_kv 127.0.0.1 7777
```

---

## 📡 Protocol

Commands are plain text, one command per line, terminated by `\r\n`.

| Command | Response |
| --- | --- |
| `PING` | `PONG` |
| `SET key value` | `OK` |
| `GET key` | `value` or `(nil)` |
| `DEL key` | `OK` or `(nil)` |
| `STATS` | `connections:N keys:N uptime:Ns` |

```bash
printf 'PING\r\nSET foo bar\r\nGET foo\r\nGET baz\r\nDEL foo\r\nGET foo\r\nSTATS\r\n' | nc 127.0.0.1 7777
```

```text
PONG
OK
bar
(nil)
OK
(nil)
connections:1 keys:0 uptime:0s
```

---

## 📊 Benchmarks

Localhost results on a 12th Gen Intel(R) Core(TM) i3-1215U, Linux 6.17.0-29-generic, liburing 2.11:

| Test | req/s | p50 ms | p99 ms |
| --- | ---: | ---: | ---: |
| SET pipeline=1 | 47,263.08 | 0.018 | 0.049 |
| SET pipeline=32 | 181,553.43 | 0.154 | 0.322 |
| GET pipeline=32 | 173,882.64 | 0.162 | 0.335 |

> 💡 Bottleneck is the Python client, not the server.
> A C++ client would push this past 500k req/s.

---

## 🏗️ Architecture

```text
main()
  └── UringLoop            ← one thread, one io_uring ring
        ├── accept_loop()  ← IORING_OP_ACCEPT, resubmits itself
        ├── read_conn()    ← IORING_OP_RECV per connection
        ├── process()      ← parses command → calls KVStore
        └── write_conn()   ← IORING_OP_SEND with response

KVStore
  └── std::unordered_map<string, string>
        + counters (connections, ops, uptime)
```

CQE dispatch uses a compact `user_data` encoding so completions can arrive out of order without losing context:

```text
user_data = (uint64_t(op_type) << 32) | uint64_t(fd)
```

The high 32 bits identify the operation (`Accept`, `Recv`, `Send`), and the low 32 bits carry the socket fd.

---

## 🧠 Design Decisions

| Decision | Choice | Why |
| --- | --- | --- |
| Threading model | Single-threaded event loop | One ring owns sockets, buffers, and store state, removing locks from the core data path. |
| I/O API | Raw `io_uring`, no Boost/ASIO | Shows direct knowledge of Linux async I/O and the SQE/CQE completion model. |
| Protocol | Custom text protocol over RESP | Keeps the scope focused and readable while accepting limited Redis compatibility. |

---

## 🔭 What's next

- Add optional multishot accept on Linux 5.19+.
- Add per-thread rings for multi-core scaling.
- Add TTL support for expiring keys.
- Explore TLS using `io_uring` fixed buffers.

---

<p align="center">
  Built with <a href="https://github.com/axboe/liburing">liburing</a> · C++20 · Linux
</p>
