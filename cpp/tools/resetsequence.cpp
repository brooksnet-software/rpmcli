// resetsequence - reset an RPM queue's sequence number (its job total) to zero,
// as Queue > Reset Total in the RPM GUI.
#include <iostream>
#include <string>

#include "common.hpp"

int main(int argc, char** argv) {
  rpmtools::Connection conn;
  const auto args = rpmtools::parseArgs(argc, argv, conn);
  if (args.size() != 1) {
    std::cerr << "usage: resetsequence [connection options] <queue>\n" << rpmtools::connectionUsage();
    return 2;
  }
  const std::string queue = args[0];

  auto r = rpmtools::connectOrExit("rpmcli resetsequence", conn);
  rpmtools::require(*r, conn, "queue-reset-total");
  const long long qid = rpmtools::queueId(*r, conn, queue);
  return rpmtools::finish(r->call("queue-reset-total", {{"qid", rpm::Json(qid)}}));
}
