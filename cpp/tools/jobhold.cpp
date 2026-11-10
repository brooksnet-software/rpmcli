// jobhold - hold or release a job by id.
#include <iostream>
#include <string>

#include "common.hpp"

int main(int argc, char** argv) {
  rpmtools::Connection conn;
  const auto args = rpmtools::parseArgs(argc, argv, conn);
  if (args.size() != 2) {
    std::cerr << "usage: jobhold [connection options] <jobid> <true|false|toggle>\n"
              << rpmtools::connectionUsage();
    return 2;
  }
  long long jobid = 0;
  try {
    size_t used = 0;
    jobid = std::stoll(args[0], &used);
    if (used != args[0].size()) throw std::invalid_argument("");
  } catch (const std::exception&) {
    std::cerr << "jobid must be an integer.\n";
    return 2;
  }
  const std::string hold = args[1];
  if (hold != "true" && hold != "false" && hold != "toggle") {
    std::cout << "Invalid Hold Option.\n";
    return 1;
  }

  auto r = rpmtools::connectOrExit("rpmcli jobhold", conn);
  rpmtools::require(*r, conn, "job-hold");
  return rpmtools::finish(r->jobHold(jobid, hold));
}
