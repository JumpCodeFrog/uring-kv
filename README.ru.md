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

`io_uring` — это completion-based async I/O интерфейс Linux: работа отправляется через SQE, а результат приходит через CQE, без привычной для `epoll` схемы "дождаться готовности, потом сделать отдельный syscall". В этом сервере `accept`, `recv` и `send` проходят через ring, без blocking syscalls в горячем пути. За счёт этого event loop остаётся компактным, а состояние соединений, буферы и обработка протокола видны явно. Проект написан как реальная backend systems portfolio работа, а не как учебная обёртка вокруг фреймворка.

---

## 🚀 Quick Start

```bash
sudo apt install liburing-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
./build/uring_kv 127.0.0.1 7777
```

---

## 📡 Protocol

Команды передаются обычным текстом: одна команда в строке, завершение строки — `\r\n`.

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

Результаты на localhost: 12th Gen Intel(R) Core(TM) i3-1215U, Linux 6.17.0-29-generic, liburing 2.11:

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

CQE dispatch использует компактное кодирование `user_data`, чтобы completions могли приходить не по порядку, но контекст операции не терялся:

```text
user_data = (uint64_t(op_type) << 32) | uint64_t(fd)
```

Старшие 32 бита определяют операцию (`Accept`, `Recv`, `Send`), младшие 32 бита хранят socket fd.

---

## 🧠 Design Decisions

| Decision | Choice | Why |
| --- | --- | --- |
| Threading model | Single-threaded event loop | Один ring владеет sockets, buffers и состоянием store, убирая locks из core data path. |
| I/O API | Raw `io_uring`, no Boost/ASIO | Показывает прямое понимание Linux async I/O и SQE/CQE completion model. |
| Protocol | Custom text protocol over RESP | Сохраняет scope маленьким и понятным, принимая ограниченную Redis compatibility. |

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
