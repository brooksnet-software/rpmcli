// common.hpp - shared startup for the standalone RPM utilities: connection
// options, where the server, RPC key and certificate fingerprint come from, and
// a connect() helper that checks them and reports what to do on failure (the
// C++ counterpart of the Python standalones package and rpmcli.settings).
//
// No RPC key ships with the utilities. Each value comes from the first of:
//   1. --host, --port, --key, --fingerprint on the command line
//   2. RPM_HOST, RPM_PORT, RPM_KEY, RPM_FINGERPRINT in the environment
//   3. %APPDATA%\rpmcli\rpmcli.ini, shared with the Python utilities:
//        [rpmcli]           host = ..., port = ...
//        [<host>:<port>]    key = ..., fingerprint = ...
// Run at a console, a missing key or fingerprint is asked for and saved there
// once the server accepts it.
#ifndef RPM_TOOLS_COMMON_HPP
#define RPM_TOOLS_COMMON_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "rpm/RPM.hpp"

namespace rpmtools {

struct Connection {
  std::string host, key, fingerprint;
  unsigned short port = 0;
};

inline const char* connectionUsage() {
  return "connection options: [--host <server>] [--port <rpc-port>] [--key <rpc-key>]\n"
         "                    [--fingerprint <server certificate fingerprint>]\n";
}

// Remove the connection options from argv into c; returns the other arguments
// (argv[0] excluded). Prints usage and exits on a malformed option.
inline std::vector<std::string> parseArgs(int argc, char** argv, Connection& c) {
  std::vector<std::string> rest;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i], value;
    std::string* target = nullptr;
    std::string name = arg.substr(0, arg.find('='));
    if (name == "--host") target = &c.host;
    else if (name == "--key") target = &c.key;
    else if (name == "--fingerprint") target = &c.fingerprint;
    else if (name == "--port") target = &value;
    else {
      rest.push_back(arg);
      continue;
    }
    if (arg.size() > name.size())
      *target = arg.substr(name.size() + 1);
    else if (i + 1 < argc)
      *target = argv[++i];
    else {
      std::cerr << name << " needs a value.\n" << connectionUsage();
      std::exit(2);
    }
    if (target == &value) {
      c.port = static_cast<unsigned short>(std::atoi(value.c_str()));
      if (c.port == 0) {
        std::cerr << "--port must be a port number.\n";
        std::exit(2);
      }
    }
  }
  return rest;
}

inline std::string envValue(const char* name) {
  char buf[1024];
  DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
  return n > 0 && n < sizeof(buf) ? std::string(buf, n) : std::string();
}

inline std::string settingsDir() {
  return envValue("APPDATA") + "\\rpmcli";
}

inline std::string settingsPath() {
  return settingsDir() + "\\rpmcli.ini";
}

inline std::string section(const Connection& c) {
  std::string s = c.host + ":" + std::to_string(c.port);
  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

inline std::string iniValue(const std::string& sect, const char* name) {
  char buf[1024];
  DWORD n = GetPrivateProfileStringA(sect.c_str(), name, "", buf, sizeof(buf), settingsPath().c_str());
  return std::string(buf, n);
}

// Fill in whatever the command line left empty.
inline void resolve(Connection& c) {
  if (c.host.empty()) c.host = envValue("RPM_HOST");
  if (c.host.empty()) c.host = iniValue("rpmcli", "host");
  if (c.host.empty()) c.host = "localhost";
  if (c.port == 0) c.port = static_cast<unsigned short>(std::atoi(envValue("RPM_PORT").c_str()));
  if (c.port == 0) c.port = static_cast<unsigned short>(std::atoi(iniValue("rpmcli", "port").c_str()));
  if (c.port == 0) c.port = 9198;
  if (c.key.empty()) c.key = envValue("RPM_KEY");
  if (c.key.empty()) c.key = iniValue(section(c), "key");
  if (c.fingerprint.empty()) c.fingerprint = envValue("RPM_FINGERPRINT");
  if (c.fingerprint.empty()) c.fingerprint = iniValue(section(c), "fingerprint");
}

inline void save(const Connection& c, const std::map<std::string, std::string>& values) {
  CreateDirectoryA(settingsDir().c_str(), nullptr);
  for (const auto& kv : values)
    WritePrivateProfileStringA(section(c).c_str(), kv.first.c_str(), kv.second.c_str(),
                               settingsPath().c_str());
  std::cerr << "Saved to " << settingsPath() << ".\n";
}

inline std::string keyHelp() {
  return "Generate an application key on the RPM server (Settings > Ports > Modify Port >\n"
         "Add Program > Generate), then pass it with --key, set RPM_KEY, or save it in\n  "
         + settingsPath() + "\n";
}

inline std::string fingerprintHelp() {
  return "rpmgui.exe --show-management-key on the RPM server shows its certificate\n"
         "fingerprint. Pass it with --fingerprint, set RPM_FINGERPRINT, or save it in\n  "
         + settingsPath() + "\n";
}

// A console to ask at. Not _isatty(): it is true for NUL too, a character device.
inline bool interactive() {
  DWORD mode;
  return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

// Read a line from the console, or print help and exit when there is none.
inline std::string ask(const std::string& prompt, const std::string& help) {
  if (!interactive()) {
    std::cerr << help;
    std::exit(1);
  }
  std::cerr << prompt;
  std::string line;
  std::getline(std::cin, line);
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(0, 1);
  return line;
}

// Connect and check the RPC key, or print guidance and exit. client is the
// name sent with app-key, e.g. "rpmcli jobadd".
inline std::unique_ptr<rpm::RPM> connectOrExit(const std::string& client, Connection c) {
  resolve(c);
  const std::string where = c.host + ":" + std::to_string(c.port);
  std::map<std::string, std::string> unsaved;  // typed in; saved once they work
  bool rejected = false;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (c.key.empty()) {
      if (!rejected) std::cerr << "No RPC key is configured for " << where << ".\n";
      c.key = unsaved["key"] = ask("RPC key: ", keyHelp());
    }
    auto r = std::make_unique<rpm::RPM>(c.host, c.port);
    r->setClient(client);
    r->setFingerprint(c.fingerprint);
    const rpm::ConnectResult res = r->connect(c.key);
    switch (res.status) {
      case rpm::ConnectStatus::Ok:
        if (!unsaved.empty()) save(c, unsaved);
        return r;
      case rpm::ConnectStatus::Unreachable:
        std::cerr << "Could not reach RPM on " << where << " (" << res.message << ").\n"
                  << "Is RPM running and listening for RPC on that port?\n";
        std::exit(1);
      case rpm::ConnectStatus::CertificateRejected:
        std::cerr << "Not connecting to RPM on " << where << ": " << res.message << ".\n";
        if (res.configured) {
          // possibly an interception; never offer to accept it here
          std::cerr << "If the server's certificate was replaced, configure the new fingerprint.\n"
                    << fingerprintHelp();
          std::exit(1);
        }
        c.fingerprint = unsaved["fingerprint"] =
            ask("Certificate fingerprint of " + where + ": ", fingerprintHelp());
        break;
      case rpm::ConnectStatus::Unauthorized:
        std::cerr << "RPM on " << where << " did not accept the RPC key: " << res.message << ".\n";
        c.key.clear();
        unsaved.erase("key");
        rejected = true;
        break;
    }
  }
  std::exit(1);
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
