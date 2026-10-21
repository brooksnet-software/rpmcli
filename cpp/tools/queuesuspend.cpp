// queuesuspend - suspend or resume an RPM queue by name.
#include <iostream>
#include <string>

#include "common.hpp"

int main(int argc, char** argv) {
  rpmtools::Connection conn;
  const auto args = rpmtools::parseArgs(argc, argv, conn);
  if (args.size() != 2) {
    std::cerr << "usage: queuesuspend [connection options] <queue> <true|false|toggle>\n"
              << rpmtools::connectionUsage();
    return 2;
  }
  const std::string queue = args[0];
  const std::string state = args[1];
  if (state != "true" && state != "false" && state != "toggle") {
    std::cout << "Invalid Suspend State\n";
    return 1;
  }

  auto r = rpmtools::connectOrExit("rpmcli queuesuspend", conn);
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
