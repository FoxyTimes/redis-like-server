# Redis-like in-memory key–value server (C)

A compact educational, Redis-inspired TCP server implemented in C, developed as part of a university systems-programming project (August–December 2025). The project is a learning implementation, **not a production Redis replacement**.

## Highlights

- TCP server with a parser for Redis Serialization Protocol (RESP)
- In-memory hash-table storage and basic Redis-style operations (including `GET`, `SET`, and `DEL`)
- Cooperative **stackful coroutines** and a user-space scheduler based on `ucontext`
- Coroutine-aware mutexes, recursive mutexes, reader–writer locks, and semaphores
- Custom memory pool with free-list allocation and adjacent-block coalescing
- Key eviction when the memory pool is under pressure
- Linux I/O primitives including `poll` and `eventfd`

## Requirements

- Linux (the code uses Linux/POSIX APIs, including `ucontext` and `eventfd`)
- C compiler with C23 support (tested with GCC 14)
- CMake 3.28 or newer
- Optional: `redis-cli` for manual protocol testing

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/repa
```

The application reads `repa.conf` from its **current working directory** and listens on TCP port `6379`. To test it, open a second terminal:

```bash
redis-cli -p 6379 SET hello world
redis-cli -p 6379 GET hello
redis-cli -p 6379 DEL hello
```

The commands above are examples for manual testing; `PING` is not implemented, and protocol coverage and edge cases are limited. Run on an isolated development machine rather than exposing this server publicly.

## Configuration

`repa.conf` contains:

| Key | Meaning |
|---|---|
| `pool_size` | Bytes allocated for the in-memory value pool |
| `table_size` | Number of hash-table buckets |
| `num_locks` | Number of coroutine-aware lock buckets |
| `num_workers` | Number of cooperative workers |

## Implementation guide

- `src/listener.c`: TCP listener and client lifecycle
- `src/protocol_parser.c`: RESP parser
- `src/database.c`: key-value database and command processing
- `src/memory_pool.c`: custom allocator and free-list management
- `src/coroutine_pool.c`: cooperative coroutine scheduling and synchronization
- `src/workers_pool.c`: worker dispatch
- `src/config.c`: configuration parsing


