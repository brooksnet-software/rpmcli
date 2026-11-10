#!/usr/bin/env python
# -*- coding: utf-8 -*-
from argparse import ArgumentParser as ArgParse
import html
import os
import re
import shutil
import sys
import uuid

from rpmcli.standalones import add_connection_options, connect, finish, queue_id, require


def rpm_data_dir():
  """RPM's settings folder on this computer, found as the RPM service finds it:
  the folder of the database named in config.xml beside RpmSrv.exe, else
  %ProgramData%\\Brooks Internet Software\\RPM."""
  default = os.path.join(os.environ.get('ProgramData', r'C:\ProgramData'), 'Brooks Internet Software', 'RPM')
  try:
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SYSTEM\CurrentControlSet\Services\rpm') as key:
      image = os.path.expandvars(winreg.QueryValueEx(key, 'ImagePath')[0]).strip()
    exe = image[1:].split('"', 1)[0] if image.startswith('"') else image[:image.lower().find('.exe') + 4]
    with open(os.path.join(os.path.dirname(exe), 'config.xml'), 'rb') as f:
      xml = f.read()
  except (ImportError, OSError, IndexError):
    return default
  try:
    text = xml.decode('utf-8')
  except UnicodeDecodeError:
    text = xml.decode('mbcs', 'replace')    # older setups wrote it in the ANSI code page
  match = re.search(r'<dbpath value="([^"]*)"', text)
  folder = os.path.dirname(html.unescape(match.group(1))) if match else ''
  return folder or default


def stage(path):
  """Copy the job file into a new folder of its own in RPM's Incoming folder.

  RPM 7's job-add reads only such a copy (as the RPM GUI makes when a file is
  dropped on a queue) or a file shared with the RPM service account; any user
  may create a folder in Incoming.  The service moves the copy into its spool
  and removes the folder.
  """
  incoming = os.path.join(rpm_data_dir(), 'Incoming')
  if not os.path.isdir(incoming):
    print("RPM's Incoming folder %s is missing. Restart the RPM service and try again." % incoming)
    sys.exit(1)
  folder = os.path.join(incoming, '{%s}' % str(uuid.uuid4()).upper())
  staged = os.path.join(folder, os.path.basename(path))
  try:
    os.mkdir(folder)
    shutil.copyfile(path, staged)
  except OSError as e:
    unstage(staged)
    print("Could not copy the job file into %s: %s" % (incoming, e))
    sys.exit(1)
  return staged


def unstage(staged):
  """Remove a staged copy the service did not take, and its folder."""
  for remove, target in ((os.remove, staged), (os.rmdir, os.path.dirname(staged))):
    try:
      remove(target)
    except OSError:
      pass


if __name__ == '__main__':
  # Parse command line options.
  A = ArgParse(description = "A Utility to Submit jobs to RPM Queues. It runs on the RPM server, "
                             "which accepts jobs over RPC only from programs on that computer.")
  A.add_argument("queue", help = "Queue name to submit to.")
  A.add_argument("job", help = "Path to data file.")
  add_connection_options(A)
  options = A.parse_args()
  if not os.path.isfile(options.job):
    print("Job File Not Found.")
    sys.exit(1)
  # Connect and check the RPC key.
  r = connect(options)
  require(r, 'job-add')
  qid = queue_id(r, options.queue)
  original = os.path.abspath(options.job)
  staged = stage(original)
  result = r.job_add(qid = qid, path = staged, **{'original-path': original})
  if not (isinstance(result, dict) and result.get('success')):
    unstage(staged)
  finish(result)
