#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse
import sys

from standalones import connect


if __name__ == '__main__':
  # Connect and validate the configured RPC key.
  r = connect()
  # Parse command line options.
  A = ArgParse(description = "A Utility to Hold or Release jobs in RPM Queues.")
  A.add_argument("jobid", help = "Job ID to Affect.", type = int)
  A.add_argument("hold", help = "Hold State - One Of (true, false, toggle).")
  options = A.parse_args()
  if options.hold not in ('true', 'false', 'toggle'):
    print("Invalid Hold Option.")
    sys.exit(1)
  print(r.job_hold(jid = options.jobid, hold = options.hold))