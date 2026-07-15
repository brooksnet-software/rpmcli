# rpmcli (C++)

A C++ port of the RPM Remote Print Manager RPC client. It speaks the same
line-delimited JSON protocol over TCP (default `localhost:9198`) as the Python
version in the parent directory.

## Layout

```
include/rpm/   Json.hpp, RPCConnection.hpp, RPM.hpp   (public headers)
src/           RPCConnection.cpp, RPM.cpp             (implementation)
tools/         jobadd, jobhold, queuesuspend, resetsequence
tests/         smoketest (read-only end-to-end check)
third_party/   nlohmann/json.hpp (vendored)
```

## JSON backend

The client never touches a JSON library directly; everything goes through
`rpm::Json` (`include/rpm/Json.hpp`), whose backend is chosen at build time:

| Backend           | How                          | Notes                                   |
|-------------------|------------------------------|-----------------------------------------|
| nlohmann (default)| `-DRPM_JSON_BACKEND=nlohmann`| vendored single header, zero setup      |
| Boost.JSON        | `-DRPM_JSON_BACKEND=boost`   | uses an installed Boost, or `-DBOOST_ROOT=<path>` for header-only mode |

## Build

```sh
# nlohmann backend (default)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Boost backend (header-only, pointing at a Boost tree)
cmake -S . -B build-boost -G "Visual Studio 17 2022" -A x64 \
      -DRPM_JSON_BACKEND=boost -DBOOST_ROOT=C:/path/to/boost
cmake --build build-boost --config Release
```

## Usage

```
jobadd <queue> <job-file>
jobhold <jobid> <true|false|toggle>
queuesuspend <queue> <true|false|toggle>
resetsequence <queue>
```

Each utility validates the configured RPC key on startup. If the server
rejects it, the tool prints the key to register on the RPM server and exits.
The built-in key in `tools/common.hpp` is a placeholder; replace it with a key
authorized on your RPM server.

`smoketest [key] [host] [port]` issues only read-only calls and is safe to run
against a live server:

```sh
./build/Release/smoketest.exe <your-rpc-key>
```

## Scope

This port covers the client library (`RPCConnection`, `RPM`) and the four
standalone utilities. The asynchronous callback/event machinery (Python's
`register`/`receiver` and `JobTracker`) is not ported yet; it would build on
the same `RPCConnection` with a background receive thread.
