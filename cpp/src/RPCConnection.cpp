#include "rpm/RPCConnection.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace rpm {
namespace {

// Reference-counted Winsock initialization. WSAStartup/WSACleanup are balanced
// across all live RPCConnection instances.
struct Winsock {
  Winsock() {
    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) throw SocketError("WSAStartup failed: " + std::to_string(rc));
  }
  ~Winsock() { WSACleanup(); }
};

void winsockRef(bool acquire) {
  static int count = 0;
  static Winsock* ws = nullptr;
  if (acquire) {
    if (count++ == 0) ws = new Winsock();
  } else {
    if (--count == 0) { delete ws; ws = nullptr; }
  }
}

}  // namespace

RPCConnection::RPCConnection(std::string host, unsigned short port, bool autoConnect)
    : host_(std::move(host)), port_(port) {
  winsockRef(true);
  if (autoConnect) connect();
}

RPCConnection::~RPCConnection() {
  disconnect();
  winsockRef(false);
}

void RPCConnection::connect() {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* result = nullptr;
  const std::string portStr = std::to_string(port_);
  if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &result) != 0)
    throw SocketError("could not resolve " + host_ + ":" + portStr);

  SOCKET s = INVALID_SOCKET;
  for (addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET) continue;
    if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
    closesocket(s);
    s = INVALID_SOCKET;
  }
  freeaddrinfo(result);

  if (s == INVALID_SOCKET)
    throw SocketError("could not connect to " + host_ + ":" + portStr +
                      " (WSA " + std::to_string(WSAGetLastError()) + ")");

  sock_ = static_cast<unsigned long long>(s);
  connected_ = true;
  buffer_.clear();
}

void RPCConnection::disconnect() {
  if (sock_ != static_cast<unsigned long long>(INVALID_SOCKET)) {
    closesocket(static_cast<SOCKET>(sock_));
    sock_ = static_cast<unsigned long long>(INVALID_SOCKET);
  }
  connected_ = false;
}

void RPCConnection::send(const Json& obj) {
  // Matches the Python client: the request JSON is written with no trailing
  // newline; the server frames responses with '\n'.
  write(obj.dump());
}

Json RPCConnection::recv() {
  std::string line = readUntil('\n');
  try {
    return Json::parse(line);
  } catch (const JsonError&) {
    // Mirror the Python fallback: surface the raw payload rather than throwing.
    return Json();
  }
}

Json RPCConnection::comm(const Json& obj) {
  send(obj);
  return recv();
}

void RPCConnection::write(const std::string& data) {
  const char* p = data.data();
  size_t remaining = data.size();
  while (remaining > 0) {
    int n = ::send(static_cast<SOCKET>(sock_), p,
                   static_cast<int>(remaining), 0);
    if (n == SOCKET_ERROR)
      throw SocketError("send failed (WSA " +
                        std::to_string(WSAGetLastError()) + ")");
    p += n;
    remaining -= static_cast<size_t>(n);
  }
}

std::string RPCConnection::readUntil(char terminator) {
  char chunk[4096];
  for (;;) {
    size_t pos = buffer_.find(terminator);
    if (pos != std::string::npos) {
      std::string line = buffer_.substr(0, pos + 1);
      buffer_.erase(0, pos + 1);
      return line;
    }
    int n = ::recv(static_cast<SOCKET>(sock_), chunk, sizeof(chunk), 0);
    if (n == 0) {                       // peer closed the connection
      connected_ = false;
      if (buffer_.empty()) throw ConnectionClosed();
      std::string rest = buffer_;
      buffer_.clear();
      return rest;
    }
    if (n == SOCKET_ERROR)
      throw SocketError("recv failed (WSA " +
                        std::to_string(WSAGetLastError()) + ")");
    buffer_.append(chunk, static_cast<size_t>(n));
  }
}

}  // namespace rpm
