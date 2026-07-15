// jobadd - submit a job file to an RPM queue by name.
#include <fstream>
#include <iostream>
#include <string>

#include "common.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: jobadd <queue> <job-file>\n";
    return 2;
  }
  const std::string queue = argv[1];
  const std::string job = argv[2];

  std::ifstream f(job);
  if (!f.good()) {
    std::cout << "Job File Not Found.\n";
    return 1;
  }
  f.close();

  auto r = rpmtools::connectOrExit();
  const auto queues = rpmtools::queueLookup(r->queueListNames());
  auto it = queues.find(queue);
  if (it == queues.end()) {
    std::cout << "Queue name not found.\n";
    return 1;
  }
  std::cout << r->jobAdd(std::stoll(it->second), job).dump() << "\n";
  return 0;
}
