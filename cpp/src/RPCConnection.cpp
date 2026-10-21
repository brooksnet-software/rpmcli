#include "rpm/RPCConnection.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "RpmTls.h"     // after winsock2.h, which must precede windows.h

#include <algorithm>
#include <cctype>
#include <mutex>
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

std::wstring widen(const std::string& text) {
  if (text.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], n);
  return wide;
}

}  // namespace

// The lock serialises the session: Schannel contexts are not thread-safe, and
// the event receiver reads while another thread sends.
struct RPCConnection::Tls {
  std::unique_ptr<RpmTls::Session> session;
  std::mutex lock;
};

bool RPCConnection::isLocal(const std::string& host) {
  std::string h = host;
  std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return h.empty() || h == "localhost" || h == "127.0.0.1" || h == "::1";
}

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
  buffer_.clear();
  tls_.reset();
  if (!fingerprint_.empty() || !isLocal(host_)) {
    try {
      handshake();
    } catch (...) {
      disconnect();
      throw;
    }
  }
  connected_ = true;
}

// Runs the TLS handshake on the freshly connected socket and checks the
// server's certificate against fingerprint_.
void RPCConnection::handshake() {
  auto creds = std::make_shared<RpmTls::Credentials>();
  std::string err;
  if (!creds->AcquireClient(&err)) throw SocketError("TLS: " + err);
  auto tls = std::make_unique<Tls>();
  tls->session = std::make_unique<RpmTls::Session>(creds, RpmTls::Session::Role::Client, widen(host_));

  const SOCKET s = static_cast<SOCKET>(sock_);
  DWORD timeout = 10000;  // for each receive during the handshake
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

  std::string out, plain;
  if (!tls->session->Start(out)) throw SocketError("TLS: " + tls->session->Error());
  for (;;) {
    if (!out.empty()) {
      sendRaw(out);
      out.clear();
    }
    if (tls->session->Open()) break;
    char buf[16384];
    int n = ::recv(s, buf, sizeof(buf), 0);
    if (n == 0)
      throw SocketError("the server closed the connection during the TLS handshake "
                        "(an RPM service older than RPM 7?)");
    if (n == SOCKET_ERROR)
      throw SocketError("TLS handshake: recv failed (WSA " + std::to_string(WSAGetLastError()) + ")");
    if (!tls->session->Feed(buf, static_cast<size_t>(n), out, plain)) {
      if (!out.empty()) ::send(s, out.data(), static_cast<int>(out.size()), 0);  // the alert
      throw SocketError("TLS: " + tls->session->Error());
    }
  }

  const std::string where = host_ + ":" + std::to_string(port_);
  const std::string presented = tls->session->PeerFingerprint();
  if (!RpmTls::SameFingerprint(presented, fingerprint_)) {
    if (fingerprint_.empty())
      throw CertificateMismatch("no certificate fingerprint is configured for " + where, presented, false);
    throw CertificateMismatch("the certificate of " + where + " has fingerprint " + presented +
                              ", not the one configured for it", presented, true);
  }

  timeout = 0;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  buffer_ += plain;
  tls_ = std::move(tls);
}

void RPCConnection::disconnect() {
  if (tls_ && sock_ != static_cast<unsigned long long>(INVALID_SOCKET)) {
    std::string out;
    {
      std::lock_guard<std::mutex> lock(tls_->lock);
      tls_->session->Shutdown(out);  // close_notify
    }
    if (!out.empty())
      ::send(static_cast<SOCKET>(sock_), out.data(), static_cast<int>(out.size()), 0);
  }
  // tls_ itself lives until the next connect(): the event receiver may still be
  // in readUntil() on another thread when RPM's destructor calls this.
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
  if (!tls_) return sendRaw(data);
  // Held while sending, so records reach the wire in the order they were made.
  std::lock_guard<std::mutex> lock(tls_->lock);
  std::string records;
  if (!tls_->session->Encrypt(data, records)) throw SocketError("TLS: " + tls_->session->Error());
  sendRaw(records);
}

void RPCConnection::sendRaw(const std::string& data) {
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
  char chunk[16384];  // a whole TLS record
  bool closed = false;
  for (;;) {
    size_t pos = buffer_.find(terminator);
    if (pos != std::string::npos) {
      std::string line = buffer_.substr(0, pos + 1);
      buffer_.erase(0, pos + 1);
      return line;
    }
    int n = closed ? 0 : ::recv(static_cast<SOCKET>(sock_), chunk, sizeof(chunk), 0);
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
    if (!tls_) {
      buffer_.append(chunk, static_cast<size_t>(n));
      continue;
    }
    std::string out, plain;
    std::lock_guard<std::mutex> lock(tls_->lock);
    const bool ok = tls_->session->Feed(chunk, static_cast<size_t>(n), out, plain);
    if (!out.empty()) sendRaw(out);
    if (!ok) throw SocketError("TLS: " + tls_->session->Error());
    buffer_ += plain;
    closed = tls_->session->PeerClosed();  // close_notify: nothing more will come
  }
}

}  // namespace rpm
