// RPCConnection.hpp - a line-delimited JSON connection to an RPM Remote Print
// Manager server, over a raw TCP socket (Winsock on Windows).
//
// This is the C++ counterpart of the Python backend.RPCConnection class.
#ifndef RPM_RPCCONNECTION_HPP
#define RPM_RPCCONNECTION_HPP

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

class RPCConnection {
public:
  explicit RPCConnection(std::string host = "localhost",
                         unsigned short port = 9198,
                         bool autoConnect = true);
  ~RPCConnection();

  RPCConnection(const RPCConnection&) = delete;
  RPCConnection& operator=(const RPCConnection&) = delete;

  void connect();      // open the socket
  void disconnect();   // close the socket
  bool connected() const { return connected_; }

  void send(const Json& obj);   // serialize and write (no trailing newline)
  Json recv();                  // read one '\n'-terminated JSON message
  Json comm(const Json& obj);   // send then recv

private:
  void write(const std::string& data);          // send all bytes
  std::string readUntil(char terminator);       // buffered read

  std::string host_;
  unsigned short port_;
  bool connected_ = false;
  std::string buffer_;

  // SOCKET is UINT_PTR on Windows; kept as an opaque integer to avoid leaking
  // <winsock2.h> into this header.
  unsigned long long sock_ = ~0ull;  // INVALID_SOCKET
};

}  // namespace rpm

#endif  // RPM_RPCCONNECTION_HPP
