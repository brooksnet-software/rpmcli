#!/usr/bin/env python
# -*- coding: utf-8 -*-

import os
import sys
from time import sleep

from rpmcli import settings
from rpmcli.RPM import RPM
from rpmcli.backend.RPCConnection import RPCConnection, CertificateMismatch

# No RPC key ships with these utilities: each site generates its own, and
# rpmcli.settings says where the utilities look for it.
KEY_HELP = ("Generate an application key on the RPM server (Settings > Ports > Modify Port >\n"
            "Add Program > Generate), then pass it with --key, set RPM_KEY, or save it in\n"
            "  %s")
FINGERPRINT_HELP = ("rpmgui.exe --show-management-key on the RPM server shows its certificate\n"
                    "fingerprint. Pass it with --fingerprint, set RPM_FINGERPRINT, or save it in\n"
                    "  %s")


# The port permission (Settings > Ports > Modify Port > Remote Permissions) that
# lets an application key run each command the utilities use.
PERMISSIONS = {'queue-list-names': 'Queue Permissions > View',
               'queue-suspend': 'Queue Permissions > Suspend',
               'queue-reset-total': 'Queue Permissions > Configure',
               'job-hold': 'Job Permissions > Hold'}
# Commands older RPM releases don't have at all.
NEWER = {'queue-reset-total'}


def add_connection_options(parser):
  """Add --host, --port, --key and --fingerprint to an ArgumentParser."""
  group = parser.add_argument_group(
    'connection', 'Defaults come from RPM_HOST, RPM_PORT, RPM_KEY and RPM_FINGERPRINT, '
                  'then %s.' % settings.path())
  group.add_argument('--host', help = 'RPM server (default localhost).')
  group.add_argument('--port', type = int, help = 'RPC port (default %d).' % settings.DEFAULT_PORT)
  group.add_argument('--key', help = 'Application key generated on the RPM server.')
  group.add_argument('--fingerprint', help = "The server's certificate fingerprint.")


def _ask(prompt, help_text):
  answer = settings.ask(prompt) if settings.interactive() else None
  if not answer:
    print(help_text % settings.path())
    sys.exit(1)
  return answer


def connect(options = None, client = None):
  """Return a ready RPM client after checking the connection and RPC key.

  ``options`` is the result of a parser given add_connection_options().
  ``client`` is the name sent to the server with app-key; by default it is
  "rpmcli" plus the running script's name, e.g. "rpmcli jobadd".

  The check runs up front so that a missing server, a rejected key or a
  certificate that doesn't match produces a clear message and a non-zero exit,
  rather than the RPM client's background thread retrying.  When run at a
  console it asks for a missing key or fingerprint and saves what works.
  """
  if client is None:
    client = 'rpmcli ' + os.path.splitext(os.path.basename(sys.argv[0]))[0]
  host, port, key, fingerprint = settings.resolve(
    *(getattr(options, name, None) for name in ('host', 'port', 'key', 'fingerprint')))
  unsaved = {}
  rejected = False
  for _ in range(3):
    try:
      probe = RPCConnection(host, port, fingerprint = fingerprint)
    except CertificateMismatch as e:
      print("Not connecting to RPM on %s:%d: %s." % (host, port, e))
      if e.expected:
        # possibly an interception; never offer to accept it here
        print("If the server's certificate was replaced, configure the new fingerprint.")
        print(FINGERPRINT_HELP % settings.path())
        sys.exit(1)
      fingerprint = unsaved['fingerprint'] = _ask(
        "Certificate fingerprint of %s:%d: " % (host, port), FINGERPRINT_HELP)
      continue
    except OSError as e:
      print("Could not reach RPM on %s:%d (%s)." % (host, port, e))
      print("Is RPM running and listening for RPC on that port?")
      sys.exit(1)
    try:
      if not key:
        if not rejected:
          print("No RPC key is configured for %s:%d." % (host, port))
        key = unsaved['key'] = _ask("RPC key: ", KEY_HELP)
      result = probe.comm({'command': 'app-key', 'key': key, 'client': client})
    finally:
      probe._disconnect()
    if isinstance(result, dict) and result.get('success'):
      break
    message = result.get('message') if isinstance(result, dict) else None
    print("RPM on %s:%d did not accept the RPC key%s." % (host, port, ': ' + message if message else ''))
    key = ''
    rejected = True
    unsaved.pop('key', None)
  else:
    sys.exit(1)
  if unsaved:
    settings.save(host, port, **unsaved)
    print("Saved to %s." % settings.path())
  # Bring up the full client (its own login will also succeed).
  r = RPM(host = host, port = port, key = key, client = client, fingerprint = fingerprint)
  while not r.ready:
    if r.error is not None:
      print(r.error)
      sys.exit(1)
    sleep(0.25)
  return r


def require(r, *commands):
  """Exit with an explanation unless the server lets this key run commands.

  RPM 7 offers an application key only what its port's permissions grant, so
  a command it refuses has no method on ``r`` at all.
  """
  missing = [c for c in commands if c not in r.commands]
  for c in missing:
    if c == 'job-add':
      print("RPM accepts job-add only from programs on the RPM server itself, connecting to a port\n"
            "whose Authorized Hosts include localhost (and not at all once RPM's license has expired).")
    elif c in NEWER:
      print("RPC port %d on %s does not offer %s. RPM releases from before %s was added don't\n"
            "have it; otherwise, on the RPM server select %s under Remote Permissions for\n"
            "that port (Settings > Ports > Modify Port), or connect to a port that permits it."
            % (r.port, r.host, c, c, PERMISSIONS[c]))
    else:
      print("RPC port %d on %s does not permit %s. On the RPM server, select %s under\n"
            "Remote Permissions for that port (Settings > Ports > Modify Port), or connect to a port\n"
            "that permits it." % (r.port, r.host, c, PERMISSIONS.get(c, 'the permission for it')))
  if missing:
    sys.exit(1)


def finish(result):
  """Print the server's reply and exit: 0 if it succeeded, else 1."""
  print(result)
  sys.exit(0 if isinstance(result, dict) and result.get('success') else 1)


def queue_id(r, name):
  """The id of the queue called name, or exit with an explanation."""
  require(r, 'queue-list-names')
  names = r.queue_list_names()
  if not (isinstance(names, dict) and names.get('success')):
    finish(names)
  # the reply maps each queue id to its name, beside "success" and "command"
  for qid, qname in names.items():
    if qid.isdigit() and qname == name:
      return int(qid)
  print("Queue name not found.")
  sys.exit(1)
