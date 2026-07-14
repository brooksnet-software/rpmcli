#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse
import os
import sys
from time import sleep

from RPM import RPM
from standalones import clikey


if __name__ == '__main__':
  # Connect using the configured CLI Key.
  r = RPM(key = clikey)
  # Wait for connection to establish.
  while not r.ready:
    sleep(1)
  # Parse command line options.
  A = ArgParse(description = "A Utility to Hold or Release jobs in RPM Queues.")
  A.add_argument("jobid", help = "Job ID to Affect.", type = int)
  A.add_argument("hold", help = "Hold State - One Of (true, false, toggle).")
  options = A.parse_args()
  if options.hold not in ('true', 'false', 'toggle'):
    print "Invalid Hold Option."
    sys.exit(1)
  print r.job_hold(jid = options.jobid, hold = options.hold)