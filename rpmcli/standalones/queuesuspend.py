#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse
import sys

from rpmcli.standalones import add_connection_options, connect, finish, queue_id, require


if __name__ == '__main__':
  # Parse command line options.
  A = ArgParse(description = "A Utility to Suspend / Resume RPM Queues.")
  A.add_argument("queue", help = "Queue name to control.")
  A.add_argument("state", help = "State - One of (true, false, toggle).")
  add_connection_options(A)
  options = A.parse_args()
  if options.state not in ('true', 'false', 'toggle'):
    print("Invalid Suspend State")
    sys.exit(1)
  # Connect and check the RPC key.
  r = connect(options)
  require(r, 'queue-suspend')
  # queue-suspend, not queue-modify: RPM does not implement queue-modify.
  finish(r.queue_suspend(qid = queue_id(r, options.queue), state = options.state))
