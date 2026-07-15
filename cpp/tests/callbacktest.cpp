// callbacktest - verify the asynchronous callback machinery and JobTracker
// against an in-process mock RPM server. No real server is contacted and no
// state is mutated.
//
// The mock authorizes any key, answers list-all-rpc, and once both callbacks
// are registered emits a scripted event sequence:
//   job.add  777          (single job-id)
//   job.add  [778, 779]   (list of job-ids)
//   job.done 777          (completes 777 -> pruned)
// Expected final tracked state: 778 and 779 retained (job.add only); 777 gone.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>

#include "rpm/JobTracker.hpp"
#include "rpm/RPM.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace {

void sendLine(SOCKET s, const std::string& line) {
  std::string out = line + "\n";
  send(s, out.data(), static_cast<int>(out.size()), 0);
}

// Pull the next brace-balanced JSON object out of buf, reading more as needed.
// (The protocol's request objects contain no nested or in-string braces.)
std::string readMsg(SOCKET s, std::string& buf) {
  for (;;) {
    int depth = 0;
    bool started = false;
    for (size_t i = 0; i < buf.size(); ++i) {
      if (buf[i] == '{') { depth++; started = true; }
      else if (buf[i] == '}') {
        if (started && --depth == 0) {
          std::string msg = buf.substr(0, i + 1);
          buf.erase(0, i + 1);
          return msg;
        }
      }
    }
    char tmp[4096];
    int n = recv(s, tmp, static_cast<int>(sizeof(tmp)), 0);
    if (n <= 0) return std::string();
    buf.append(tmp, static_cast<size_t>(n));
  }
}

void connHandler(SOCKET s) {
  std::string buf;
  for (;;) {
    std::string msg = readMsg(s, buf);
    if (msg.empty()) break;
    if (msg.find("list-all-rpc") != std::string::npos) {
      sendLine(s, R"({"commands":["job-add","queue-modify"],"success":true})");
    } else if (msg.find("app-key") != std::string::npos) {
      sendLine(s, R"({"success":true,"command":"app-key"})");
    } else if (msg.find("callback-add") != std::string::npos) {
      // Emit events only once both callbacks are registered, so the tracker's
      // event set is complete before any event is delivered (deterministic).
      if (msg.find("job.done") != std::string::npos) {
        sendLine(s, R"({"callback":"job.add","job-id":777})");
        sendLine(s, R"({"callback":"job.add","job-id":[778,779]})");
        sendLine(s, R"({"callback":"job.done","job-id":777})");
      }
    }
    // callback-remove and anything else: ignore.
  }
  closesocket(s);
}

void acceptor(SOCKET listener) {
  for (;;) {
    SOCKET c = accept(listener, nullptr, nullptr);
    if (c == INVALID_SOCKET) break;
    std::thread(connHandler, c).detach();
  }
}

}  // namespace

int main() {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);

  SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;  // ephemeral
  bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  listen(listener, 4);
  int len = sizeof(addr);
  getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
  const unsigned short port = ntohs(addr.sin_port);
  std::thread(acceptor, listener).detach();
  std::cout << "[mock RPM server on 127.0.0.1:" << port << "]\n";

  rpm::RPM r("127.0.0.1", port);
  const rpm::ConnectResult res = r.connect("testkey");
  if (!res.ok()) {
    std::cerr << "connect failed: " << res.message << "\n";
    return 1;
  }
  std::cout << "connected; commands loaded: " << r.commands().size() << "\n";

  rpm::JobTracker jt(r);
  jt.add("job.add");
  jt.add("job.done");

  // Wait for the scripted events to be dispatched and reach steady state.
  std::map<std::string, std::set<std::string>> jobs;
  for (int i = 0; i < 60; ++i) {
    jobs = jt.jobs();
    if (jobs.count("778") && jobs.count("779") && !jobs.count("777")) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::cout << "tracked jobs after events:\n";
  for (const auto& kv : jobs) {
    std::cout << "  " << kv.first << " ->";
    for (const auto& e : kv.second) std::cout << " " << e;
    std::cout << "\n";
  }

  auto has = [&](const std::string& id, const std::string& ev) {
    auto it = jobs.find(id);
    return it != jobs.end() && it->second.count(ev) > 0;
  };

  bool ok = true;
  ok &= (jobs.count("777") == 0);   // received job.add + job.done -> pruned
  ok &= has("778", "job.add");      // list job-id dispatched and retained
  ok &= has("779", "job.add");
  ok &= (jobs.size() == 2);

  std::cout << (ok
      ? "PASS: dispatch, list job-ids, retain-incomplete, and prune all work\n"
      : "FAIL\n");
  return ok ? 0 : 1;
}
