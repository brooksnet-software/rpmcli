#include "rpm/RPM.hpp"

namespace rpm {

RPM::RPM(std::string host, unsigned short port)
    : conn_(std::move(host), port, /*autoConnect=*/false) {}

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
