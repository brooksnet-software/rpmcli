// JobTracker.hpp - monitor a set of events on a per-job basis.
//
// A job that has received *every* tracked event is dropped; all others are
// retained along with the events seen so far. C++ counterpart of the Python
// JobTracker.
#ifndef RPM_JOBTRACKER_HPP
#define RPM_JOBTRACKER_HPP

#include <map>
#include <mutex>
#include <set>
#include <string>

#include "rpm/Json.hpp"
#include "rpm/RPM.hpp"

namespace rpm {

class JobTracker {
public:
  explicit JobTracker(RPM& rpm) : rpm_(rpm) {}

  // Track an RPM callback (e.g. "job.add") and subscribe to it.
  void add(const std::string& callback);

  // Snapshot of currently tracked jobs: job-id -> the events received so far.
  std::map<std::string, std::set<std::string>> jobs() const;

private:
  void handle(const Json& data);   // invoked on the RPM receiver thread
  void prune();                    // caller holds mutex_

  RPM& rpm_;
  mutable std::mutex mutex_;
  std::set<std::string> events_;
  std::map<std::string, std::set<std::string>> jobs_;
};

}  // namespace rpm

#endif  // RPM_JOBTRACKER_HPP
