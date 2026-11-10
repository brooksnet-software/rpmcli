#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse

from rpmcli.standalones import add_connection_options, connect, finish, queue_id, require


if __name__ == '__main__':
  # Parse command line options.
  A = ArgParse(description = "A Utility to Reset RPM Sequence Numbers (a queue's job total, "
                             "as Queue > Reset Total in the RPM GUI).")
  A.add_argument("queue", help = "Queue name to reset.")
  add_connection_options(A)
  options = A.parse_args()
  # Connect and check the RPC key.
  r = connect(options)
  require(r, 'queue-reset-total')
  finish(r.queue_reset_total(qid = queue_id(r, options.queue)))
