#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse
import sys

from rpmcli.standalones import connect


if __name__ == '__main__':
  # Connect and validate the configured RPC key.
  r = connect()
  # Parse command line options.
  A = ArgParse(description = "A Utility to Suspend / Resume RPM Queues.")
  A.add_argument("queue", help = "Queue name to control.")
  A.add_argument("state", help = "State - One of (true, false, toggle).")
  options = A.parse_args()
  if options.state not in ('true', 'false', 'toggle'):
    print("Invalid Suspend State")
    sys.exit(1)
  # Create a reverse lookup for Queue names.
  queues = {str(v): k for k, v in r.queue_list_names().items()}
  states = {'true': True,
            'false': False,
            'toggle': 'toggle'}
  if options.queue not in queues:
    print("Queue name not found.")
    sys.exit(1)
  print(r.queue_modify(qid = int(queues[options.queue]), suspend = states[options.state]))
