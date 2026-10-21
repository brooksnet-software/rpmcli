#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse

from rpmcli.standalones import add_connection_options, connect


if __name__ == '__main__':
  # Parse command line options.
  A = ArgParse(description = "A Utility to Reset RPM Sequence Numbers.")
  A.add_argument("queue", help = "Queue name to reset.")
  add_connection_options(A)
  options = A.parse_args()
  # Connect and check the RPC key.
  r = connect(options)
  # Create a reverse lookup for Queue names.
  queues = {str(v): k for k, v in r.queue_list_names().items()}
  if options.queue not in queues:
    print("Queue name not found.")
  else:
    print(r.queue_modify(qid = int(queues[options.queue]), seqno = 0))
