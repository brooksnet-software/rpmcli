#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys
from time import sleep

from rpmcli.RPM import RPM
from rpmcli.backend.RPCConnection import RPCConnection

# RPC key these utilities present to the RPM server.  It must be registered as
# an authorized key on the RPM server before the utilities will work; connect()
# validates it on startup and tells the user to add it if the server says no.
clikey = '8c14c198-f6cb-4a13-be97-33214ea969f2'


def _report_bad_key(host, port, message):
  print("RPM on %s:%d did not accept the configured RPC key." % (host, port))
  if message:
    print("  server response: %s" % message)
  print("  key: %s" % clikey)
  print("Add this key to your RPM server's authorized RPC keys, then re-run.")


def connect(host = 'localhost', port = 9198):
  """Return a ready RPM client after validating the configured RPC key.

  Performs a synchronous key check up front so that a missing server or an
  unauthorized key produces a clear message and a non-zero exit, rather than
  the RPM client's background thread retrying (or prompting) forever.
  """
  # Pre-flight: reach the server and check the key directly.
  try:
    probe = RPCConnection(host, port)
  except OSError as e:
    print("Could not reach RPM on %s:%d (%s)." % (host, port, e))
    print("Is RPM running and listening for RPC on that port?")
    sys.exit(1)
  try:
    result = probe.comm({'command': 'app-key', 'key': clikey})
  finally:
    probe._disconnect()
  if not isinstance(result, dict) or not result.get('success'):
    message = result.get('message') if isinstance(result, dict) else None
    _report_bad_key(host, port, message)
    sys.exit(1)
  # Key is valid; bring up the full client (its own auth will also succeed).
  r = RPM(host = host, port = port, key = clikey)
  while not r.ready:
    sleep(0.25)
  return r
