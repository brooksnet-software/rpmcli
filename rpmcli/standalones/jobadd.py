#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse
import os
import sys

from rpmcli.standalones import add_connection_options, connect


if __name__ == '__main__':
  # Parse command line options.
  A = ArgParse(description = "A Utility to Submit jobs to RPM Queues.")
  A.add_argument("queue", help = "Queue name to submit to.")
  A.add_argument("job", help = "Path to data file.")
  add_connection_options(A)
  options = A.parse_args()
  # Connect and check the RPC key.
  r = connect(options)
  if not os.path.isfile(options.job):
    print("Job File Not Found.")
    sys.exit(1)
  # Create a reverse lookup for Queue names.
  queues = {str(v): k for k, v in r.queue_list_names().items()}
  if options.queue not in queues:
    print("Queue name not found.")
    sys.exit(1)
  print(r.job_add(qid = int(queues[options.queue]), path = options.job))
