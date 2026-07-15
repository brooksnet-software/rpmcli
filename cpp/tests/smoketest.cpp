// smoketest - read-only end-to-end check against a live RPM server.
//
//   smoketest [key] [host] [port]
//
// Connects, authorizes, loads the command list, and prints queue names. It
// only issues read-only RPC calls (app-key, list-all-rpc, queue-list-names),
// so it is safe to run against a production server. The key is taken from the
// command line so no credential needs to be compiled in.
#include <cstdlib>
#include <iostream>
#include <string>

#include "rpm/RPM.hpp"

int main(int argc, char** argv) {
  const std::string key  = argc > 1 ? argv[1] : "8c14c198-f6cb-4a13-be97-33214ea969f2";
  const std::string host = argc > 2 ? argv[2] : "localhost";
  const unsigned short port =
      static_cast<unsigned short>(argc > 3 ? std::atoi(argv[3]) : 9198);

#if defined(RPM_JSON_BOOST)
  std::cout << "[backend: boost::json]\n";
#else
  std::cout << "[backend: nlohmann/json]\n";
#endif

  rpm::RPM r(host, port);
  const rpm::ConnectResult res = r.connect(key);
  if (!res.ok()) {
    std::cerr << "connect failed ("
              << (res.status == rpm::ConnectStatus::Unreachable ? "unreachable"
                                                                : "unauthorized")
              << "): " << res.message << "\n";
    return 1;
  }

  std::cout << "connected; server exposes " << r.commands().size()
            << " rpc commands\n";

  const rpm::Json names = r.queueListNames();
  std::cout << "queues:\n";
  for (const auto& kv : names.objectItems())
    if (kv.second.isString())
      std::cout << "  " << kv.first << " -> " << kv.second.asString() << "\n";

  return 0;
}
