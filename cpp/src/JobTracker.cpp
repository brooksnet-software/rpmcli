#include "rpm/JobTracker.hpp"

#include <algorithm>

namespace rpm {

namespace {
// A job-id may arrive as a number or a string; normalize to a string key.
std::string jobKey(const Json& j) {
  return j.isString() ? j.asString() : std::to_string(j.asInt());
}
}  // namespace

void JobTracker::add(const std::string& callback) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.insert(callback);
  }
  // Register outside the lock: registerCallback takes RPM's own lock.
  rpm_.registerCallback(callback, [this](const Json& data) { handle(data); });
}

void JobTracker::handle(const Json& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string callback = data.at("callback").asString();
  const Json jid = data.at("job-id");
  if (jid.isArray()) {
    for (const auto& j : jid.items()) jobs_[jobKey(j)].insert(callback);
  } else {
    jobs_[jobKey(jid)].insert(callback);
  }
  prune();
}

void JobTracker::prune() {
  // Drop each job that has now received every tracked event
  // (i.e. events_ is a subset of the events the job has seen).
  for (auto it = jobs_.begin(); it != jobs_.end();) {
    if (std::includes(it->second.begin(), it->second.end(),
                      events_.begin(), events_.end()))
      it = jobs_.erase(it);
    else
      ++it;
  }
}

std::map<std::string, std::set<std::string>> JobTracker::jobs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return jobs_;
}

}  // namespace rpm
