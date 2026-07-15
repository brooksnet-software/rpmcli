#!/usr/bin/env python
# -*- coding: utf-8 -*-

from collections import defaultdict

class JobTracker(object):
  '''
    This class is intended to monitor a set of events on a per-job
    basis.  Jobs that have received *all* events will be removed.
    All other jobs will be stored along with the events they have received.
  '''
  jobs = defaultdict(set)
  events = set()

  def __init__(self, rpm):
    self.r = rpm

  def handler(self, data):
    # Track this event for this job.
    if isinstance(data['job-id'], list):
      for jid in data['job-id']:
        self.jobs[jid].add(data['callback'])
    else:
      self.jobs[data['job-id']].add(data['callback'])
    # Remove jobs that have received all registered events.
    self.prune()

  def add(self, callback):
    self.events.add(callback)
    self.r.register(callback, self.handler)

  def prune(self):
    for jid in list(self.jobs.keys()):
      if not self.events - self.jobs[jid]:
        self.jobs.pop(jid)
