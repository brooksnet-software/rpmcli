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
  rpmtools::require(*r, conn, "queue-suspend");
  const long long qid = rpmtools::queueId(*r, conn, queue);
  // queue-suspend, not queue-modify: RPM does not implement queue-modify.
  return rpmtools::finish(r->call("queue-suspend", {{"qid", rpm::Json(qid)}, {"state", rpm::Json(state)}}));
}
