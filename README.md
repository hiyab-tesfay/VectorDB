# MiniVectorDB

A small C++20 vector search engine built from scratch. It implements
Hierarchical Navigable Small World (HNSW) search without FAISS, hnswlib, or other
runtime dependencies.

## Features

- Dynamic HNSW insertion and approximate nearest-neighbor search
- Cosine and squared-L2 distance
- Exact search for measuring recall
- Simple binary save/load format
- CLI, tests, and benchmark

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI

Input files contain one vector per line: `ID value1 value2 ...`. Blank lines are
allowed; malformed IDs and rows with the wrong number of values are rejected.

```bash
./build/minivdb build index.bin 3 cosine vectors.txt 12 80
./build/minivdb add index.bin 42 0.2 0.8 0.1
./build/minivdb query index.bin 5 50 0.1 0.9 0.0
./build/minivdb stats index.bin
```

Search returns `ID<TAB>distance`, ordered from nearest to farthest.

## C++ API

```cpp
#include "minivdb.hpp"

minivdb::Index index({384, minivdb::Metric::cosine, 12, 80, 42});
index.add(7, embedding);
auto hits = index.search(query, 10, 64);
index.save("index.bin");
```

## How it works

Each vector becomes a node in several randomly selected graph layers. Insertion
greedily descends the upper layers, performs a bounded candidate search, and links
the new node to nearby neighbors. Queries use the same upper-layer descent before
a wider layer-zero search controlled by `ef_search`.

Larger `M`, `ef_construction`, and `ef_search` generally improve recall while using
more memory or computation. Run `./build/minivdb_bench` to compare HNSW throughput
and recall against the exact linear scan.

This is an educational, single-process index—not a production database. It does
not include concurrency, metadata filtering, replication, or crash-safe storage.

## Simple fuzz test

`fuzz/fuzz.cpp` saves a small valid index, flips one random bit in a fresh copy,
and tries loading and searching it. It repeats this 1,000 times. Rejecting a bad
file is expected; AddressSanitizer and UndefinedBehaviorSanitizer stop the run
if they detect an error. The scratch file is placed beside the executable and
removed after the run. Run one instance at a time.

After the normal build:

```bash
./build/minivdb_fuzz
```

This is a small mutation test. It does not measure code coverage, and mutations
can repeat. CI is configured to run it alongside the regular tests.
