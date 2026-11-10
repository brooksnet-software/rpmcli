# rpmcli (C++)

A C++ port of the RPM Remote Print Manager RPC client. It speaks the same
line-delimited JSON protocol over TCP (default `localhost:9198`) as the Python
version in the parent directory.

## Layout

```
include/rpm/   Json.hpp, RPCConnection.hpp, RPM.hpp, JobTracker.hpp  (public headers)
src/           RPCConnection.cpp, RPM.cpp, JobTracker.cpp            (implementation)
tools/         jobadd, jobhold, queuesuspend, resetsequence
tests/         smoketest (read-only live check), callbacktest (mock-server)
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
jobadd [connection options] <queue> <job-file>
jobhold [connection options] <jobid> <true|false|toggle>
queuesuspend [connection options] <queue> <true|false|toggle>
resetsequence [connection options] <queue>

connection options: [--host <server>] [--port <rpc-port>] [--key <rpc-key>]
                    [--fingerprint <server certificate fingerprint>]
```

No RPC key is built in. Generate one for each program on the RPM server
(Settings > Ports > Modify Port > Add Program > Generate). RPM 7 accepts
connections from other computers only over TLS, and the utilities check the
server's certificate against a fingerprint before sending the key.
`rpmgui.exe --show-management-key` on the server shows two, and either works:
the certificate fingerprint, and the public key fingerprint, which stays the
same when the certificate is renewed with the same key. A server on this
computer is reached without TLS unless a fingerprint is given.

Each value comes from the command line, then the environment (`RPM_HOST`,
`RPM_PORT`, `RPM_KEY`, `RPM_FINGERPRINT`), then `%APPDATA%\rpmcli\rpmcli.ini`,
which the Python utilities share:

```ini
[rpmcli]
host = rpmserver

[rpmserver:9198]
key = <application key>
fingerprint = AB:CD:...
```

Run at a console, a utility asks for a missing key or fingerprint and saves it
there once the server accepts it. It never offers to accept a certificate that
differs from the configured fingerprint. The utilities send `"client":
"rpmcli <tool>"` with the key, so RPM's Events log names them.

### Permissions

An application key may run only what its RPC port's Remote Permissions grant
(Settings > Ports > Modify Port); `RPM::commands()` and `RPM::permits()` say
which, and the utilities check before calling and name the permission that is
missing. Each exits with 1 when RPM refuses the request.

| Utility         | Port permissions                         | Notes |
|-----------------|------------------------------------------|-------|
| `jobadd`        | Queue Permissions > View                 | Runs on the RPM server: RPM offers `job-add` only to programs there. RPM 7 reads only a copy in its `Incoming` folder, so `jobadd` copies the file there; the original stays put. |
| `jobhold`       | Job Permissions > Hold                   | |
| `queuesuspend`  | Queue Permissions > View, Suspend        | Uses `queue-suspend`. |
| `resetsequence` | Queue Permissions > View, Configure      | Uses `queue-reset-total`, as Queue > Reset Total in the GUI; RPM releases from before it was added don't have it. |

`smoketest <key> [host] [port] [fingerprint]` issues only read-only calls and
is safe to run against a live server:

```sh
./build/Release/smoketest.exe <your-rpc-key> rpmserver 9198 AB:CD:...
```

## Asynchronous callbacks

`RPM::registerCallback(name, handler)` subscribes to an RPM event (e.g.
`"job.add"`). The first registration opens a second "conduit" connection and
starts a background receiver thread that dispatches events to the registered
handlers. Because `std::function` is not comparable, handlers are identified by
the id returned from `registerCallback` (rather than by function identity as in
Python); pass it to `unregisterCallback`. RPM 7 refuses an application key a
subscription its port does not permit (and some that need the management key);
the receiver then drops that callback's handlers and passes the server's reply
to the function given to `setErrorHandler` (by default, it writes to stderr). `JobTracker` builds on this to track
which events each job has received, dropping jobs once they have seen every
tracked event.

## Scope

This port covers the full client library (`RPCConnection`, `RPM`, the async
callback machinery and `JobTracker`) and the four standalone utilities.
