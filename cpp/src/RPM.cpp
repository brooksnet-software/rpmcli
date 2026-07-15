#include "rpm/RPM.hpp"

#include <chrono>
#include <iostream>
#include <vector>

namespace rpm {

RPM::RPM(std::string host, unsigned short port)
    : conn_(host, port, /*autoConnect=*/false),
      host_(std::move(host)),
      port_(port) {}

RPM::~RPM() {
  // Stop the receiver thread: flag it, then close the conduit so a blocked
  // recv() returns, then join.
  receiverStop_ = true;
  if (conduit_) conduit_->disconnect();
  if (receiver_.joinable()) receiver_.join();
}

ConnectResult RPM::connect(const std::string& key) {
  key_ = key;
  try {
    conn_.connect();
  } catch (const SocketError& e) {
    return {ConnectStatus::Unreachable, e.what()};
  }

  Json auth = appKey(key);
  if (!auth.at("success").asBool())
    return {ConnectStatus::Unauthorized,
            auth.at("message").asString("authorization failed")};

  loadcmds();
  ready_ = true;
  return {ConnectStatus::Ok, ""};
}

Json RPM::appKey(const std::string& key) {
  Json params = Json::object();
  params.set("key", key);
  return command("app-key", params);
}

void RPM::loadcmds() {
  Json resp = command("list-all-rpc");
  for (const auto& c : resp.at("commands").items())
    commands_.insert(c.asString());
}

Json RPM::command(const std::string& cmd, const Json& params) {
  std::lock_guard<std::mutex> lock(mutex_);
  Json req = Json::object();
  req.set("command", cmd);
  for (const auto& kv : params.objectItems()) req.set(kv.first, kv.second);
  return conn_.comm(req);
}

Json RPM::call(const std::string& cmd, const std::map<std::string, Json>& kwargs) {
  Json params = Json::object();
  std::set<std::string> consumed;

  const auto& spec = methodspec();
  auto it = spec.find(cmd);
  if (it != spec.end()) {
    for (const auto& mapping : it->second) {           // argname -> rpcname
      auto k = kwargs.find(mapping.first);
      if (k != kwargs.end()) {
        params.set(mapping.second, k->second);
        consumed.insert(mapping.first);
      }
    }
  }
  if (addkwargs().count(cmd)) {
    for (const auto& kv : kwargs)
      if (!consumed.count(kv.first)) params.set(kv.first, kv.second);
  }
  return command(cmd, params);
}

Json RPM::queueListNames() { return command("queue-list-names"); }

Json RPM::jobAdd(long long qid, const std::string& path) {
  return call("job-add", {{"qid", Json(qid)}, {"path", Json(path)}});
}

Json RPM::jobHold(long long jid, const std::string& hold) {
  return call("job-hold", {{"jid", Json(jid)}, {"hold", Json(hold)}});
}

Json RPM::queueModify(const std::map<std::string, Json>& kwargs) {
  return call("queue-modify", kwargs);
}

// ----------------------------------------------------------------------------
// Asynchronous event callbacks (Python's register/unregister/receiver).
// ----------------------------------------------------------------------------
void RPM::setCloseHandler(std::function<void()> fn) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  closeHandler_ = std::move(fn);
}

RPM::HandlerId RPM::registerCallback(const std::string& callback,
                                     EventHandler handler) {
  std::lock_guard<std::mutex> lock(cbMutex_);

  // First registration: open the conduit, authorize it, start the receiver.
  // The app-key exchange completes before the receiver thread starts so the
  // two never read from the conduit concurrently.
  if (!conduit_) {
    conduit_.reset(new RPCConnection(host_, port_));
    Json auth = Json::object();
    auth.set("command", "app-key");
    auth.set("key", key_);
    conduit_->comm(auth);
    receiverStop_ = false;
    receiver_ = std::thread(&RPM::receiverLoop, this);
  }

  // Only send callback-add the first time a callback name is registered; RPM
  // reports an error if the same callback is added twice.
  auto& handlers = callbacks_[callback];
  const bool firstForName = handlers.empty();
  const HandlerId id = nextHandlerId_++;
  handlers[id] = std::move(handler);

  if (firstForName) {
    Json add = Json::object();
    add.set("command", "callback-add");
    add.set("callback", callback);
    conduit_->send(add);
  }
  return id;
}

void RPM::unregisterCallback(const std::string& callback, HandlerId id) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  auto it = callbacks_.find(callback);
  if (it == callbacks_.end()) return;
  it->second.erase(id);
  // Keep the subscription while other handlers still listen for this event.
  if (!it->second.empty()) return;
  callbacks_.erase(it);
  if (conduit_) {
    Json rm = Json::object();
    rm.set("command", "callback-remove");
    rm.set("callback", callback);
    conduit_->send(rm);
  }
}

void RPM::receiverLoop() {
  for (;;) {
    Json data;
    try {
      data = conduit_->recv();
    } catch (const ConnectionClosed&) {
      if (!receiverStop_ && closeHandler_) closeHandler_();
      break;
    } catch (const SocketError&) {
      break;  // conduit closed (typically during shutdown)
    }
    if (receiverStop_) break;
    if (!data.isObject() || !data.contains("callback")) continue;

    const std::string callback = data.at("callback").asString();
    // Copy the current handlers under the lock, then dispatch outside it so a
    // handler that calls back into RPM cannot deadlock.
    std::vector<EventHandler> handlers;
    {
      std::lock_guard<std::mutex> lock(cbMutex_);
      auto it = callbacks_.find(callback);
      if (it != callbacks_.end())
        for (const auto& kv : it->second) handlers.push_back(kv.second);
    }
    for (const auto& h : handlers) safecall(h, data, callback);
  }
}

void RPM::safecall(const EventHandler& handler, const Json& data,
                   const std::string& name) {
  // Allow "unreliable" handlers: retry a few times before giving up.
  for (int attempt = 0; attempt < 5; ++attempt) {
    try {
      handler(data);
      return;
    } catch (...) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  std::cerr << "Unable to successfully call handler for " << name << "\n";
}

// ----------------------------------------------------------------------------
// Static command metadata, ported verbatim from the Python RPM class.
// ----------------------------------------------------------------------------
const std::map<std::string, std::map<std::string, std::string>>& RPM::methodspec() {
  static const std::map<std::string, std::map<std::string, std::string>> spec = {
    {"action-add",           {{"atype", "action-type"}}},
    {"action-device-id",     {{"aid", "action-id"}}},
    {"action-enable",        {{"aid", "action-id"}, {"enable", "enable"}}},
    {"action-get",           {{"aid", "action-id"}}},
    {"action-get-devmode",   {{"aid", "action-id"}}},
    {"action-get-queue",     {{"aid", "action-id"}}},
    {"action-list",          {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"action-list-fields",   {{"atype", "action-type"}}},
    {"action-modify",        {{"aid", "action-id"}}},
    {"action-refresh",       {{"aid", "action-id"}}},
    {"action-refresh-all",   {{"qid", "queue-id"}}},
    {"action-remove",        {{"aid", "action-id"}}},
    {"action-remove-all",    {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"action-set-devmode",   {{"aid", "action-id"}, {"devmode", "devmode"}}},
    {"action-set-type",      {{"atype", "action-type"}, {"aid", "action-id"}}},
    {"app-status",           {{"option", "option"}}},
    {"callback-add",         {{"callback", "callback"}}},
    {"callback-call",        {{"tag", "callback-tag"}}},
    {"callback-remove",      {{"callback", "callback"}}},
    {"device-add",           {{"path", "path"}, {"dtype", "device-type"}}},
    {"device-alarm",         {{"did", "device-id"}}},
    {"device-error",         {{"did", "device-id"}, {"error", "error"}}},
    {"device-get",           {{"did", "device-id"}}},
    {"device-get-path",      {{"path", "path"}}},
    {"device-get-state",     {{"did", "device-id"}}},
    {"device-get-status",    {{"did", "device-id"}}},
    {"device-get-user",      {{"did", "device-id"}}},
    {"device-modify",        {{"did", "device-id"}}},
    {"device-refresh",       {{"did", "device-id"}}},
    {"device-remove",        {{"did", "device-id"}}},
    {"device-remove-path",   {{"path", "path"}}},
    {"device-set-state",     {{"did", "device-id"}, {"state", "state"}}},
    {"device-set-status",    {{"did", "device-id"}, {"status", "status"}}},
    {"device-set-user",      {{"did", "device-id"}, {"user", "user"}}},
    {"files-get",            {{"path", "path"}}},
    {"folder-create",        {{"path", "path"}, {"leaf", "leaf"}}},
    {"job-add",              {{"qid", "queue-id"}, {"qname", "queue-name"}, {"path", "job-path"}}},
    {"job-cancel",           {{"jid", "job-id"}}},
    {"job-clear-error",      {{"jid", "job-id"}}},
    {"job-copy",             {{"jid", "job-id"}}},
    {"job-file-str",         {{"jid", "job-id"}, {"mask", "string"}}},
    {"job-find",             {{"name", "name"}}},
    {"job-get",              {{"jid", "job-id"}}},
    {"job-get-error",        {{"jid", "job-id"}}},
    {"job-get-path",         {{"jid", "job-id"}}},
    {"job-hold",             {{"jid", "job-id"}, {"hold", "hold"}}},
    {"job-modify",           {{"jid", "job-id"}}},
    {"job-move",             {{"jid", "job-id"}}},
    {"job-remove",           {{"jid", "job-id"}}},
    {"job-rename",           {{"jid", "job-id"}, {"jname", "job-name"}}},
    {"job-reprint",          {{"jid", "job-id"}}},
    {"logon-batch-add",      {{"uid", "user-id"}}},
    {"network-host-2-ip",    {{"host", "host"}}},
    {"network-ip-2-host",    {{"ip", "ip"}}},
    {"network-is-ip",        {{"ip", "ip"}}},
    {"port-refresh",         {{"port", "port"}}},
    {"port-remove",          {{"ports", "ports"}}},
    {"printer-devmode",      {{"printer", "printer"}}},
    {"queue-add",            {{"qname", "queue-name"}}},
    {"queue-eligible",       {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-enable",         {{"qid", "queue-id"}, {"state", "enable"}}},
    {"queue-exists",         {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-find",           {{"name", "name"}}},
    {"queue-get",            {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-hold",           {{"qid", "queue-id"}, {"state", "hold"}}},
    {"queue-id",             {{"qname", "queue-name"}}},
    {"queue-is-archiving",   {{"qid", "queue-id"}}},
    {"queue-is-enabled",     {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-is-held",        {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-is-suspended",   {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-jobs",           {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-modify",         {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-name",           {{"qid", "queue-id"}}},
    {"queue-purge",          {{"qid", "queue-id"}}},
    {"queue-refresh",        {{"qid", "queue-id"}}},
    {"queue-remove",         {{"qid", "queue-id"}}},
    {"queue-remove-archive", {{"qid", "queue-id"}}},
    {"queue-rename",         {{"qid", "queue-id"}, {"qname", "queue-name"}, {"newname", "newname"}}},
    {"queue-reprint",        {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-seqno",          {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"queue-set-archive",    {{"qid", "queue-id"}, {"policy", "delete-jobs"}}},
    {"queue-suspend",        {{"qid", "queue-id"}, {"state", "suspend"}}},
    {"scheduler2-job-action-status", {{"jid", "job-id"}, {"aid", "action-id"}}},
    {"scheduler2-job-status",        {{"jid", "job-id"}}},
    {"settings-get",             {{"category", "category"}, {"attr", "attr"}}},
    {"settings-list",            {{"category", "category"}}},
    {"settings-refresh-category",{{"category", "category"}}},
    {"settings-remove",          {{"category", "category"}, {"attr", "attr"}}},
    {"settings-remove-category", {{"category", "category"}}},
    {"settings-set",             {{"category", "category"}, {"attr", "attr"}, {"val", "value"}}},
    {"socket-add-listener",   {{"port", "port"}, {"proto", "protocol"}}},
    {"socket-get-port",       {{"port", "port"}}},
    {"socket-modify-listener",{{"port", "port"}, {"proto", "protocol"}}},
    {"socket-port-start",     {{"port", "port"}}},
    {"socket-port-stop",      {{"port", "port"}}},
    {"spool-alarm",           {{"path", "dir"}}},
    {"spool-exist",           {{"path", "path"}}},
    {"spool-set-dir",         {{"path", "dir"}}},
    {"spool-set-temp",        {{"path", "dir"}}},
    {"spool-unique-path",     {{"path", "dir"}, {"name", "file"}}},
    {"transform-add",         {{"ttype", "transform-type"}}},
    {"transform-enable",      {{"tid", "transform-id"}, {"enable", "enable"}}},
    {"transform-get",         {{"tid", "transform-id"}}},
    {"transform-list",        {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"transform-list-fields", {{"ttype", "transform-type"}}},
    {"transform-modify",      {{"tid", "transform-id"}}},
    {"transform-refresh",     {{"tid", "transform-id"}}},
    {"transform-refresh-all", {{"qid", "queue-id"}}},
    {"transform-remove",      {{"tid", "transform-id"}}},
    {"transform-remove-all",  {{"qid", "queue-id"}, {"qname", "queue-name"}}},
    {"user-add",              {{"user", "user"}, {"pwd", "password"}, {"domain", "domain"}}},
    {"user-get",              {{"uid", "user-id"}}},
    {"user-modify",           {{"uid", "user-id"}, {"uname", "user-name"}}},
    {"user-refresh",          {{"uid", "user-id"}}},
    {"user-remove",           {{"uid", "user-id"}}},
    {"user-reset",            {{"uids", "user-ids"}}},
  };
  return spec;
}

const std::set<std::string>& RPM::addkwargs() {
  static const std::set<std::string> kw = {
    "action-add", "action-modify", "callback-call", "device-add",
    "device-modify", "job-add", "job-find", "job-modify", "port-modify",
    "queue-find", "queue-modify", "queue-rename", "socket-modify-listener",
    "transform-add", "transform-modify", "user-modify",
  };
  return kw;
}

}  // namespace rpm
