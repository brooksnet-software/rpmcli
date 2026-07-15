// queuesuspend - suspend or resume an RPM queue by name.
#include <iostream>
#include <string>

#include "common.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: queuesuspend <queue> <true|false|toggle>\n";
    return 2;
  }
  const std::string queue = argv[1];
  const std::string state = argv[2];
  if (state != "true" && state != "false" && state != "toggle") {
    std::cout << "Invalid Suspend State\n";
    return 1;
  }

  auto r = rpmtools::connectOrExit();
  const auto queues = rpmtools::queueLookup(r->queueListNames());
  auto it = queues.find(queue);
  if (it == queues.end()) {
    std::cout << "Queue name not found.\n";
    return 1;
  }

  // true/false become booleans; toggle stays a string (matches the Python CLI).
  rpm::Json suspend = (state == "toggle") ? rpm::Json("toggle")
                                          : rpm::Json(state == "true");
  std::cout << r->queueModify({{"qid", rpm::Json(std::stoll(it->second))},
                               {"suspend", suspend}}).dump()
            << "\n";
  return 0;
}
