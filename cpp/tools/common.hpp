// common.hpp - shared startup for the standalone RPM utilities: the built-in
// RPC key and a connect() helper that validates it and reports what to do on
// failure (the C++ counterpart of the Python standalones package).
#ifndef RPM_TOOLS_COMMON_HPP
#define RPM_TOOLS_COMMON_HPP

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>

#include "rpm/RPM.hpp"

namespace rpmtools {

// RPC key these utilities present to the RPM server. It must be registered as
// an authorized key on the RPM server before the utilities will work; connect()
// validates it on startup and tells the user to add it if the server says no.
inline const char* clikey() {
  return "8c14c198-f6cb-4a13-be97-33214ea969f2";
}

// Connect and validate the configured RPC key, or print guidance and exit.
inline std::unique_ptr<rpm::RPM> connectOrExit(const std::string& host = "localhost",
                                               unsigned short port = 9198) {
  auto r = std::make_unique<rpm::RPM>(host, port);
  const std::string key = clikey();
  const rpm::ConnectResult res = r->connect(key);

  if (res.status == rpm::ConnectStatus::Unreachable) {
    std::cerr << "Could not reach RPM on " << host << ":" << port
              << " (" << res.message << ").\n"
              << "Is RPM running and listening for RPC on that port?\n";
    std::exit(1);
  }
  if (res.status == rpm::ConnectStatus::Unauthorized) {
    std::cerr << "RPM on " << host << ":" << port
              << " did not accept the configured RPC key.\n"
              << "  server response: " << res.message << "\n"
              << "  key: " << key << "\n"
              << "Add this key to your RPM server's authorized RPC keys, then re-run.\n";
    std::exit(1);
  }
  return r;
}

// Build a queue-name -> queue-id lookup from a queue-list-names response.
inline std::map<std::string, std::string> queueLookup(const rpm::Json& names) {
  std::map<std::string, std::string> queues;
  for (const auto& kv : names.objectItems())
    if (kv.second.isString()) queues[kv.second.asString()] = kv.first;
  return queues;
}

}  // namespace rpmtools

#endif  // RPM_TOOLS_COMMON_HPP
