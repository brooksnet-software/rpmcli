'''
Created on Mar 8, 2011

@author: daniel
'''

import json
import socket

class RPCConnection(object):
  """A line-delimited JSON connection to an RPM Remote Print Manager server.

  Previously built on telnetlib.Telnet, which was removed from the standard
  library in Python 3.13.  This is a direct socket reimplementation that keeps
  the same public interface (send / recv / comm) and buffering behavior.
  """

  def __init__(self, host = 'localhost', port = 9198, auto = True):
    self.host, self.port, self.connected = host, port, False
    self.sock = None
    self.debuglevel = 0
    self._buffer = b''
    if auto:
      self._connect()

  def _connect(self):
    self.sock = socket.create_connection((self.host, self.port))
    self.connected = True

  def _disconnect(self):
    if self.sock is not None:
      self.sock.close()
      self.sock = None
    self.connected = False

  def __enter__(self):
    self._connect()
    return self

  def __exit__(self, etype, value, traceback):
    self._disconnect()

  def set_debuglevel(self, level):
    self.debuglevel = level

  def msg(self, msg, *args):
    if self.debuglevel > 0:
      print('RPCConnection(%s,%s):' % (self.host, self.port),
            msg % args if args else msg)

  def send(self, adict):
    self.write(json.dumps(adict))

  def recv(self):
    resp = self.read_until(b'\n')
    self.msg('recv %r', resp)
    try:
      return json.loads(resp)
    except Exception as e:
      print(e)
      print(repr(resp))
      return resp

  def comm(self, adict):
    self.send(adict)
    return self.recv()

  def read_until(self, terminator):
    """Read from the socket until ``terminator`` is seen.

    Returns the bytes read, including the terminator.  Raises EOFError when
    the connection is closed before any further data arrives, matching the
    telnetlib.Telnet.read_until semantics that RPM.receiver relies on to
    detect a dropped connection.
    """
    if isinstance(terminator, str):
      terminator = terminator.encode('utf-8')
    while terminator not in self._buffer:
      chunk = self.sock.recv(4096)
      if not chunk:  # remote closed the connection
        self.connected = False
        if not self._buffer:
          raise EOFError('connection closed by remote host')
        data, self._buffer = self._buffer, b''
        return data
      self._buffer += chunk
    idx = self._buffer.index(terminator) + len(terminator)
    data, self._buffer = self._buffer[:idx], self._buffer[idx:]
    return data

  def write(self, buffer):
    """Write a string (or bytes) to the socket.

    Can block if the connection is blocked.  May raise socket.error if the
    connection is closed.  JSON text is encoded to UTF-8 before sending.
    """
    if isinstance(buffer, str):
      buffer = buffer.encode('utf-8')
    self.msg("send %r", buffer)
    self.sock.sendall(buffer)



if __name__ == "__main__":
  with RPCConnection() as conn:
    print(conn.comm({'command': 'make-uuid'}))
