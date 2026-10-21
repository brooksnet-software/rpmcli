// jobadd - submit a job file to an RPM queue by name.
#include <fstream>
#include <iostream>
#include <string>

#include "common.hpp"

int main(int argc, char** argv) {
  rpmtools::Connection conn;
  const auto args = rpmtools::parseArgs(argc, argv, conn);
  if (args.size() != 2) {
    std::cerr << "usage: jobadd [connection options] <queue> <job-file>\n" << rpmtools::connectionUsage();
    return 2;
  }
  const std::string queue = args[0];
  const std::string job = args[1];

  std::ifstream f(job);
  if (!f.good()) {
    std::cout << "Job File Not Found.\n";
    return 1;
  }
  f.close();

  auto r = rpmtools::connectOrExit("rpmcli jobadd", conn);
  const auto queues = rpmtools::queueLookup(r->queueListNames());
  auto it = queues.find(queue);
  if (it == queues.end()) {
    std::cout << "Queue name not found.\n";
    return 1;
  }
  std::cout << r->jobAdd(std::stoll(it->second), job).dump() << "\n";
  return 0;
}
