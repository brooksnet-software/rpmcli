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

#include <map>
#include <mutex>
#include <set>
#include <string>

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

  static const std::map<std::string, std::map<std::string, std::string>>& methodspec();
  static const std::set<std::string>& addkwargs();

private:
  Json appKey(const std::string& key);
  void loadcmds();

  RPCConnection conn_;
  std::string key_;
  std::set<std::string> commands_;
  bool ready_ = false;
  std::mutex mutex_;
};

}  // namespace rpm

#endif  // RPM_RPM_HPP
