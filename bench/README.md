# Benchmarks

## Environment

CPU: 12th Gen Intel(R) Core(TM) i3-1215U
Kernel: 6.17.0-29-generic
liburing: 2.11

## Results

| Test              | req/s     | p50 ms | p99 ms |
|-------------------|-----------|--------|--------|
| SET pipeline=1    | 47,263.08 | 0.018  | 0.049  |
| SET pipeline=32   | 181,553.43 | 0.154 | 0.322  |
| GET pipeline=32   | 173,882.64 | 0.162 | 0.335  |

## Notes

Single-threaded io_uring event loop, localhost only.
Bottleneck at this scale is likely the Python client, not the server.
