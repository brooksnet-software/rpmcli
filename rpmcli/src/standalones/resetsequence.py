#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse
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
  A = ArgParse(description = "A Utility to Reset RPM Sequence Numbers.")
  A.add_argument("queue", help = "Queue name to reset.")
  options = A.parse_args()
  # Create a reverse lookup for Queue names.
  queues = {unicode(v): k for k, v in r.queue_list_names().items()}
  if options.queue not in queues:
    print "Queue name not found."
  else:
    print r.queue_modify(qid = int(queues[options.queue]), seqno = 0)
