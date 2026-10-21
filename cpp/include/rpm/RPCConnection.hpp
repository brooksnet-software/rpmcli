// RPCConnection.hpp - a line-delimited JSON connection to an RPM Remote Print
// Manager server, over a raw TCP socket (Winsock on Windows).
//
// This is the C++ counterpart of the Python backend.RPCConnection class.
//
// RPM 7 accepts connections from other computers only over TLS (Schannel). The
// server's certificate is checked against the fingerprint set with
// setFingerprint() before anything is sent; a server on this computer is
// reached without TLS unless a fingerprint is set.
#ifndef RPM_RPCCONNECTION_HPP
#define RPM_RPCCONNECTION_HPP

#include <memory>
#include <string>
#include <stdexcept>

#include "rpm/Json.hpp"

namespace rpm {

// Thrown when the peer closes the connection (mirrors Python's EOFError, which
// RPM's event receiver relies on to detect a dropped connection).
struct ConnectionClosed : std::runtime_error {
  ConnectionClosed() : std::runtime_error("connection closed by remote host") {}
};

// Thrown on socket-level failures (connect/send/recv).
struct SocketError : std::runtime_error {
  explicit SocketError(const std::string& what) : std::runtime_error(what) {}
};

// Thrown by connect() when the server's certificate is not the one expected, or
// no fingerprint was set. presented is the fingerprint it showed; nothing was
// sent to it.
struct CertificateMismatch : SocketError {
  CertificateMismatch(const std::string& what, std::string presentedFp, bool wasConfigured)
      : SocketError(what), presented(std::move(presentedFp)), configured(wasConfigured) {}
  std::string presented;
  bool configured;
};

class RPCConnection {
public:
  explicit RPCConnection(std::string host = "localhost",
                         unsigned short port = 9198,
                         bool autoConnect = true);
  ~RPCConnection();

  RPCConnection(const RPCConnection&) = delete;
  RPCConnection& operator=(const RPCConnection&) = delete;

  // The server certificate's SHA-256 fingerprint ("AB:CD:...", as
  // rpmgui.exe --show-management-key shows it). Call before connect().
  void setFingerprint(std::string fingerprint) { fingerprint_ = std::move(fingerprint); }
  // This computer, which RPM lets connect without TLS.
  static bool isLocal(const std::string& host);

  void connect();      // open the socket (and TLS)
  void disconnect();   // close the socket
  bool connected() const { return connected_; }

  void send(const Json& obj);   // serialize and write (no trailing newline)
  Json recv();                  // read one '\n'-terminated JSON message
  Json comm(const Json& obj);   // send then recv

private:
  void write(const std::string& data);          // send all bytes (encrypted)
  void sendRaw(const std::string& data);        // send all bytes as they are
  void handshake();
  std::string readUntil(char terminator);       // buffered read

  std::string host_;
  unsigned short port_;
  bool connected_ = false;
  std::string buffer_;
  std::string fingerprint_;
  struct Tls;                  // Schannel session, kept out of this header
  std::unique_ptr<Tls> tls_;

  // SOCKET is UINT_PTR on Windows; kept as an opaque integer to avoid leaking
  // <winsock2.h> into this header.
  unsigned long long sock_ = ~0ull;  // INVALID_SOCKET
};

}  // namespace rpm

#endif  // RPM_RPCCONNECTION_HPP
