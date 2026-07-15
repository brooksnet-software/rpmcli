#!/usr/bin/env python
# -*- coding: utf-8 -*-

'''
Created on Jun 10, 2013

@author: daniel
'''
from code import interact
from collections import deque, defaultdict
from contextlib import closing
import shelve
from threading import Thread, Lock
from time import sleep

from rpmcli.backend.RPCConnection import RPCConnection


class RPM(object):
  """
    The RPM Class encapsulates an RPC Connection to a server
    running RPM Remote Print Manager.
  """

  methodspec = {
    'action-add': {'atype': 'action-type'},
    'action-device-id': {'aid': 'action-id'},
    'action-enable': {'aid': 'action-id', 'enable': 'enable'},
    'action-get': {'aid': 'action-id'},
    'action-get-devmode': {'aid': 'action-id'},
    'action-get-queue': {'aid': 'action-id'},
    'action-list': {'qid': 'queue-id', 'qname': 'queue-name'},
    'action-list-fields': {'atype': 'action-type'},
    'action-modify': {'aid': 'action-id'},
    'action-refresh': {'aid': 'action-id'},
    'action-refresh-all': {'qid': 'queue-id'},
    'action-remove': {'aid': 'action-id'},
    'action-remove-all': {'qid': 'queue-id', 'qname': 'queue-name'},
    'action-set-devmode': {'aid': 'action-id', 'devmode': 'devmode'},
    'action-set-type': {'atype': 'action-type', 'aid': 'action-id'},
    'app-status': {'option': 'option'},
    'callback-add': {'callback': 'callback'},
    'callback-call': {'tag': 'callback-tag'},
    'callback-remove': {'callback': 'callback'},
    'device-add': {'path': 'path', 'dtype': 'device-type'},
    'device-alarm': {'did': 'device-id'},
    'device-error': {'did': 'device-id', 'error': 'error'},
    'device-get': {'did': 'device-id'},
    'device-get-path': {'path': 'path'},
    'device-get-state': {'did': 'device-id'},
    'device-get-status': {'did': 'device-id'},
    'device-get-user': {'did': 'device-id'},
    'device-modify': {'did': 'device-id'},
    'device-refresh': {'did': 'device-id'},
    'device-remove': {'did': 'device-id'},
    'device-remove-path': {'path': 'path'},
    'device-set-state': {'did': 'device-id', 'state': 'state'},
    'device-set-status': {'did': 'device-id', 'status': 'status'},
    'device-set-user': {'did': 'device-id', 'user': 'user'},
    'files-get': {'path': 'path'},
    'folder-create': {'path': 'path', 'leaf': 'leaf'},
    'job-add': {'qid': 'queue-id', 'qname': 'queue-name', 'path': 'job-path'},
    'job-cancel': {'jid': 'job-id'},
    'job-clear-error': {'jid': 'job-id'},
    'job-copy': {'jid': 'job-id'},
    'job-file-str': {'jid': 'job-id', 'mask': 'string'},
    'job-find': {'name': 'name'},
    'job-get': {'jid': 'job-id'},
    'job-get-error': {'jid': 'job-id'},
    'job-get-path': {'jid': 'job-id'},
    'job-hold': {'jid': 'job-id', 'hold': 'hold'},
    'job-modify': {'jid': 'job-id'},
    'job-move': {'jid': 'job-id'},
    'job-remove': {'jid': 'job-id'},
    'job-rename': {'jid': 'job-id', 'jname': 'job-name'},
    'job-reprint': {'jid': 'job-id'},
    'logon-batch-add': {'uid': 'user-id'},
    'network-host-2-ip': {'host': 'host'},
    'network-ip-2-host': {'ip': 'ip'},
    'network-is-ip': {'ip': 'ip'},
    'port-refresh': {'port': 'port'},
    'port-remove': {'ports': 'ports'},
    'printer-devmode': {'printer': 'printer'},
    'queue-add': {'qname': 'queue-name'},
    'queue-eligible': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-enable': {'qid': 'queue-id', 'state': 'enable'},
    'queue-exists': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-find': {'name': 'name'},
    'queue-get': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-hold': {'qid': 'queue-id', 'state': 'hold'},
    'queue-id': {'qname': 'queue-name'},
    'queue-is-archiving': {'qid': 'queue-id'},
    'queue-is-enabled': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-is-held': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-is-suspended': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-jobs': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-modify': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-name': {'qid': 'queue-id'},
    'queue-purge': {'qid': 'queue-id'},
    'queue-refresh': {'qid': 'queue-id'},
    'queue-remove': {'qid': 'queue-id'},
    'queue-remove-archive': {'qid': 'queue-id'},
    'queue-rename': {'qid': 'queue-id', 'qname': 'queue-name', 'newname': 'newname'},
    'queue-reprint': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-seqno': {'qid': 'queue-id', 'qname': 'queue-name'},
    'queue-set-archive': {'qid': 'queue-id', 'policy': 'delete-jobs'},
    'queue-suspend': {'qid': 'queue-id', 'state': 'suspend'},
    'scheduler2-job-action-status': {'jid': 'job-id', 'aid': 'action-id'},
    'scheduler2-job-status': {'jid': 'job-id'},
    'settings-get': {'category': 'category', 'attr': 'attr'},
    'settings-list': {'category': 'category'},
    'settings-refresh-category': {'category': 'category'},
    'settings-remove': {'category': 'category', 'attr': 'attr'},
    'settings-remove-category': {'category': 'category'},
    'settings-set': {'category': 'category', 'attr': 'attr', 'val': 'value'},
    'socket-add-listener': {'port': 'port', 'proto': 'protocol'},
    'socket-get-port': {'port': 'port'},
    'socket-modify-listener': {'port': 'port', 'proto': 'protocol'},
    'socket-port-start': {'port': 'port'},
    'socket-port-stop': {'port': 'port'},
    'spool-alarm': {'path': 'dir'},
    'spool-exist': {'path': 'path'},
    'spool-set-dir': {'path': 'dir'},
    'spool-set-temp': {'path': 'dir'},
    'spool-unique-path': {'path': 'dir', 'name': 'file'},
    'transform-add': {'ttype': 'transform-type'},
    'transform-enable': {'tid': 'transform-id', 'enable': 'enable'},
    'transform-get': {'tid': 'transform-id'},
    'transform-list': {'qid': 'queue-id', 'qname': 'queue-name'},
    'transform-list-fields': {'ttype': 'transform-type'},
    'transform-modify': {'tid': 'transform-id'},
    'transform-refresh': {'tid': 'transform-id'},
    'transform-refresh-all': {'qid': 'queue-id'},
    'transform-remove': {'tid': 'transform-id'},
    'transform-remove-all': {'qid': 'queue-id', 'qname': 'queue-name'},
    'user-add': {'user': 'user', 'pwd': 'password', 'domain': 'domain'},
    'user-get': {'uid': 'user-id'},
    'user-modify': {'uid': 'user-id', 'uname': 'user-name'},
    'user-refresh': {'uid': 'user-id'},
    'user-remove': {'uid': 'user-id'},
    'user-reset': {'uids': 'user-ids'}
  }
  addkwargs = {'action-add',
               'action-modify',
               'callback-call',
               'device-add',
               'device-modify',
               'job-add',
               'job-find',
               'job-modify',
               'port-modify',
               'queue-find',
               'queue-modify',
               'queue-rename',
               'socket-modify-listener',
               'transform-add',
               'transform-modify',
               'user-modify'}

  def __init__(self,
               host = 'localhost',
               port = 9198,
               key = None,
               closehandler = None):
    self.host = host
    self.port = port
    self.key = key
    self.conn = None
    self.ready = False
    self._bgconnect(True)
    self.conduit = None
    self.closehandler = closehandler
    self.L = Lock()
    self.logfunc = None
    
  def _bgconnect(self, start = False):
    if start:
      t = Thread(target = self._bgconnect)
      t.daemon = True
      return t.start()
    while True:
      try:
        self.conn = RPCConnection(self.host, self.port)
        self.auth(self.key)
        return self.loadcmds()
      except:
        pass

  def auth(self, key):
    if key is not None:
      self.rpckey = key
      authorized = self.app_key()
      if authorized['success']:
        return authorized
    with closing(shelve.open('constants')) as store:
      mykeystr = '%s-%d-%s' % (self.host, self.port, 'rpckey')
      self.rpckey = store.get(mykeystr, '')
      while True:
        authorized = self.app_key()
        if authorized['success']:
          return authorized
        print(authorized['message'])
        store[mykeystr] = self.rpckey = input('Please enter your RPC Key: ')

  def _gen(self, cmd):
    def func(self):
      return self.command(cmd)
    func.__doc__ = "Method: %s\n -- No Additional Keywords --" % cmd
    return func

  def _genkwargs(self, cmd):
    def func(self, **kwargs):
      params = {rpcname: kwargs.pop(argname)
                  for argname, rpcname in self.methodspec[cmd].items()
                    if argname in kwargs}
      if cmd in self.addkwargs:
        params.update(kwargs)
      return self.command(cmd, **params)
    argstr = '\n'.join(map(': '.join, sorted(self.methodspec[cmd].items())))
    if cmd in self.addkwargs:
      argstr += '\n**kwargs: Additional unlisted parameters allowed.'
    func.__doc__ = "Method: %s\n -- Keyword Mappings --\n%s" % (cmd, argstr)
    return func

  def loadcmds(self):
    for cmd in self.command('list-all-rpc')['commands']:
      method = cmd.replace('-', '_')
      # Prevent customized defintions from being overriden.
      if hasattr(self, method):
        continue
      genfunc = self._genkwargs if cmd in self.methodspec else self._gen
      func = genfunc(cmd)
      func.__name__ = str(method)
      # hyphens aren't valid in python syntax, but they're
      # accessible via getattr if necessary.
      setattr(RPM, cmd, func)
      setattr(RPM, method, func)
    self.ready = True
    
  def register(self, callback, func = None):
    """
      The first call to this function sets up a secondary connection
      to the RPM Server to handle async event notifications.

      Events are collected into a deque for fast appends.
    """
    if self.conduit is None:
      self.conduit = RPCConnection(self.host, self.port)
      self.conduit.comm(dict(command = 'app-key', key = self.rpckey))
      self.events = deque()
      self.responses = deque()
      self.errors = deque()
      self.callbacks = defaultdict(set)
      self.receiver(start = True)
    if callback in self.callbacks:
      self.callbacks[callback].add(func)
      return
    # Accessing here just initializes an empty set.  This prevents
    # multiple redundant calls from re-sending the callback-add, which
    # RPM reports an error to because the callback is already defined.
    if func is not None:
      self.callbacks[callback].add(func)
    else:
      self.callbacks[callback]
    self.conduit.send(dict(command = 'callback-add', callback = callback))

  def unregister(self, callback, func = None):
    if func is not None:
      self.callbacks[callback].discard(func)
    # don't actually unregister if there are other functions listening
    # for this event.
    if self.callbacks[callback]:
      return
    self.callbacks.pop(callback)
    self.conduit.send(dict(command = 'callback-remove', callback = callback))

  def receiver(self, start = False):
    if start:
      t = Thread(target = self.receiver, name = 'Event Receiver')
      t.daemon = True
      return t.start()
    while True:
      try:
        data = self.conduit.recv()
      except EOFError:
        if self.closehandler is not None:
          self.closehandler()
        break
#       if 'success' in data:
#         self.responses.append(data)
      if not isinstance(data, dict):
        print("Errant JSON String -", data)
        continue
      if 'callback' in data:
#         self.events.append(data)
        callback = data['callback']
        for handler in list(self.callbacks[callback]):
          self.safecall(handler, data)
            # Don't continue to call faulty handlers.
#             self.callbacks[callback].discard(handler)
#             self.errors.append(format_exc())

  def safecall(self, handler, data):
    # This approach allows for registration of "unreliable" Callbacks.
    for _ in range(5):
      try:
        handler(data)
      except:
        sleep(0.1)
        continue
      break
    else:
      print("Unable to successfully call %s" % handler.__name__)

  def app_key(self):
    return self.command('app-key', key = self.rpckey)

  def setlogfunc(self, func):
    self.logfunc = func

  def command(self, cmd, **kwargs):
    with self.L:
      sending = dict(command=cmd, **kwargs)
      if self.logfunc is not None:
        self.logfunc('Sending - %s' % sending)
      return self.conn.comm(sending)

if __name__ == '__main__':
  r = RPM()
  interact(local = globals())
