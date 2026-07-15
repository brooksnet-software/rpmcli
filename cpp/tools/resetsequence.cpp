// resetsequence - reset an RPM queue's sequence number to zero.
#include <iostream>
#include <string>

#include "common.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: resetsequence <queue>\n";
    return 2;
  }
  const std::string queue = argv[1];

  auto r = rpmtools::connectOrExit();
  const auto queues = rpmtools::queueLookup(r->queueListNames());
  auto it = queues.find(queue);
  if (it == queues.end()) {
    std::cout << "Queue name not found.\n";
    return 1;
  }
  std::cout << r->queueModify({{"qid", rpm::Json(std::stoll(it->second))},
                               {"seqno", rpm::Json(0)}}).dump()
            << "\n";
  return 0;
}
