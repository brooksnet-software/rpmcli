// RPM.hpp - a client for RPM Remote Print Manager's RPC interface.
//
// C++ counterpart of the Python RPM class. Python generates a method per RPC
// command at runtime; C++ cannot, so the equivalent surface is:
//
//   command(cmd, params)  - low-level: send {command: cmd, ...params}
//   call(cmd, kwargs)     - map abbreviated kwargs to RPC field names via the
//                           methodspec table (what the generated methods did)
//
// plus a few thin convenience wrappers for the commands the CLI tools use.
#ifndef RPM_RPM_HPP
#define RPM_RPM_HPP

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "rpm/Json.hpp"
#include "rpm/RPCConnection.hpp"

namespace rpm {

enum class ConnectStatus { Ok, Unreachable, Unauthorized };

struct ConnectResult {
  ConnectStatus status = ConnectStatus::Unreachable;
  std::string message;                 // server/socket detail on failure
  bool ok() const { return status == ConnectStatus::Ok; }
};

class RPM {
public:
  explicit RPM(std::string host = "localhost", unsigned short port = 9198);
  ~RPM();

  RPM(const RPM&) = delete;
  RPM& operator=(const RPM&) = delete;

  // Connect, authorize with the RPC key, and load the command list.
  ConnectResult connect(const std::string& key);
  bool ready() const { return ready_; }

  // Low-level request: {command: cmd, ...params}.
  Json command(const std::string& cmd, const Json& params = Json::object());

  // Friendly request: abbreviated kwargs (e.g. "qid") are renamed to their RPC
  // fields (e.g. "queue-id") per methodspec; for commands in addkwargs, unknown
  // kwargs are passed through unchanged.
  Json call(const std::string& cmd, const std::map<std::string, Json>& kwargs);

  // Convenience wrappers used by the standalone utilities.
  Json queueListNames();
  Json jobAdd(long long qid, const std::string& path);
  Json jobHold(long long jid, const std::string& hold);
  Json queueModify(const std::map<std::string, Json>& kwargs);

  const std::set<std::string>& commands() const { return commands_; }

  // ---- asynchronous event callbacks ----------------------------------------
  using EventHandler = std::function<void(const Json&)>;
  using HandlerId = std::size_t;

  // Register a handler for an RPM callback (e.g. "job.add"). The first
  // registration opens a secondary "conduit" connection and starts a background
  // receiver thread. Returns an id used to unregister this handler.
  //
  // Note: unlike the Python API (which keys handlers by function identity),
  // std::function is not comparable, so handlers are identified by the returned
  // id instead.
  HandlerId registerCallback(const std::string& callback, EventHandler handler);

  // Remove a previously registered handler. When the last handler for a
  // callback is removed, callback-remove is sent to the server.
  void unregisterCallback(const std::string& callback, HandlerId id);

  // Set a function invoked if the event connection is closed by the server.
  void setCloseHandler(std::function<void()> fn);

  static const std::map<std::string, std::map<std::string, std::string>>& methodspec();
  static const std::set<std::string>& addkwargs();

private:
  Json appKey(const std::string& key);
  void loadcmds();
  void receiverLoop();
  void safecall(const EventHandler& handler, const Json& data,
                const std::string& name);

  RPCConnection conn_;
  std::string host_;
  unsigned short port_;
  std::string key_;
  std::set<std::string> commands_;
  bool ready_ = false;
  std::mutex mutex_;

  // Asynchronous event delivery over a second connection.
  std::unique_ptr<RPCConnection> conduit_;
  std::thread receiver_;
  std::atomic<bool> receiverStop_{false};
  std::mutex cbMutex_;
  std::map<std::string, std::map<HandlerId, EventHandler>> callbacks_;
  HandlerId nextHandlerId_ = 1;
  std::function<void()> closeHandler_;
};

}  // namespace rpm

#endif  // RPM_RPM_HPP
