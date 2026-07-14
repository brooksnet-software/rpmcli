#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse
import os
import sys

from standalones import connect


if __name__ == '__main__':
  # Connect and validate the configured RPC key.
  r = connect()
  # Parse command line options.
  A = ArgParse(description = "A Utility to Submit jobs to RPM Queues.")
  A.add_argument("queue", help = "Queue name to submit to.")
  A.add_argument("job", help = "Path to data file.")
  options = A.parse_args()
  if not os.path.isfile(options.job):
    print("Job File Not Found.")
    sys.exit(1)
  # Create a reverse lookup for Queue names.
  queues = {str(v): k for k, v in r.queue_list_names().items()}
  if options.queue not in queues:
    print("Queue name not found.")
    sys.exit(1)
  print(r.job_add(qid = int(queues[options.queue]), path = options.job))
