// jobhold - hold or release a job by id.
#include <iostream>
#include <string>

#include "common.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: jobhold <jobid> <true|false|toggle>\n";
    return 2;
  }
  long long jobid = 0;
  try {
    size_t used = 0;
    jobid = std::stoll(argv[1], &used);
    if (used != std::string(argv[1]).size()) throw std::invalid_argument("");
  } catch (const std::exception&) {
    std::cerr << "jobid must be an integer.\n";
    return 2;
  }
  const std::string hold = argv[2];
  if (hold != "true" && hold != "false" && hold != "toggle") {
    std::cout << "Invalid Hold Option.\n";
    return 1;
  }

  auto r = rpmtools::connectOrExit();
  std::cout << r->jobHold(jobid, hold).dump() << "\n";
  return 0;
}
