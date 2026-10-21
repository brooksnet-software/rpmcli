'''
Created on Mar 8, 2011

@author: daniel
'''

import hashlib
import json
import socket
import ssl

from rpmcli.settings import is_local


def normalize_fingerprint(text):
  """The 64 hex digits of a SHA-256 fingerprint in upper case, whatever
  separators or case it was typed with; '' when it is not one."""
  digits = ''.join(c for c in (text or '') if c not in ': -	').upper()
  if len(digits) != 64 or any(c not in '0123456789ABCDEF' for c in digits):
    return ''
  return digits


def certificate_fingerprint(der):
  """SHA-256 of a certificate as "AB:CD:...", the form RPM shows."""
  digest = hashlib.sha256(der).hexdigest().upper()
  return ':'.join(digest[i:i + 2] for i in range(0, len(digest), 2))


class CertificateMismatch(Exception):
  """The server's certificate is not the one expected (or none was given).

  ``presented`` is the fingerprint the server showed.  Nothing was sent to it.
  """
  def __init__(self, host, port, presented, expected):
    self.host, self.port, self.presented, self.expected = host, port, presented, expected
    if expected:
      text = 'the certificate of %s:%d has fingerprint %s, not the one configured for it' % (host, port, presented)
    else:
      text = 'no certificate fingerprint is configured for %s:%d' % (host, port)
    Exception.__init__(self, text)

class RPCConnection(object):
  """A line-delimited JSON connection to an RPM Remote Print Manager server.

  Previously built on telnetlib.Telnet, which was removed from the standard
  library in Python 3.13.  This is a direct socket reimplementation that keeps
  the same public interface (send / recv / comm) and buffering behavior.

  RPM 7 accepts connections from other computers only over TLS.  The server's
  certificate is checked against ``fingerprint`` (see rpmcli.settings) before
  anything is sent, and CertificateMismatch is raised if it differs.  A server
  on this computer is reached without TLS unless a fingerprint is given.
  """

  def __init__(self, host = 'localhost', port = 9198, auto = True, fingerprint = ''):
    self.host, self.port, self.connected = host, port, False
    self.fingerprint = fingerprint or ''
    self.sock = None
    self.debuglevel = 0
    self._buffer = b''
    if auto:
      self._connect()

  def _connect(self):
    sock = socket.create_connection((self.host, self.port))
    if self.fingerprint or not is_local(self.host):
      try:
        sock = self._tls(sock)
      except BaseException:
        sock.close()
        raise
    self.sock = sock
    self.connected = True

  def _tls(self, sock):
    # Certificate authorities don't vouch for RPM's self-signed certificate;
    # the fingerprint check below does that job.
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    sock = context.wrap_socket(sock, server_hostname = self.host)
    presented = certificate_fingerprint(sock.getpeercert(binary_form = True))
    expected = normalize_fingerprint(self.fingerprint)
    if not expected or normalize_fingerprint(presented) != expected:
      raise CertificateMismatch(self.host, self.port, presented, self.fingerprint)
    self.msg('TLS %s, certificate %s', sock.version(), presented)
    return sock

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
