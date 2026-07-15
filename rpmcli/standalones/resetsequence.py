#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse

from rpmcli.standalones import connect


if __name__ == '__main__':
  # Connect and validate the configured RPC key.
  r = connect()
  # Parse command line options.
  A = ArgParse(description = "A Utility to Reset RPM Sequence Numbers.")
  A.add_argument("queue", help = "Queue name to reset.")
  options = A.parse_args()
  # Create a reverse lookup for Queue names.
  queues = {str(v): k for k, v in r.queue_list_names().items()}
  if options.queue not in queues:
    print("Queue name not found.")
  else:
    print(r.queue_modify(qid = int(queues[options.queue]), seqno = 0))
