"""Where rpmcli finds the RPM server, its RPC key and its certificate fingerprint.

Nothing secret ships with rpmcli.  Each site generates its own application key
in RPM (Settings > Ports > Modify Port > Add Program > Generate), and reads the
server's certificate fingerprint with ``rpmgui.exe --show-management-key`` on
the server.  The fingerprint is not secret; it lets the client check that it
is talking to that server before it sends the key.

Each value is taken from the first of these that has it:

  1. what the caller passes (the standalone utilities' --host, --port, --key
     and --fingerprint options)
  2. the environment: RPM_HOST, RPM_PORT, RPM_KEY, RPM_FINGERPRINT
  3. the settings file, %APPDATA%\\rpmcli\\rpmcli.ini (~/.config/rpmcli/rpmcli.ini
     elsewhere):

       [rpmcli]
       host = rpmserver
       port = 9198

       [rpmserver:9198]
       key = <application key>
       fingerprint = AB:CD:...

The C++ utilities read the same file, so a key saved by one works for the other.
The file is in the user's profile and readable by that user; treat it like any
other file holding a password.
"""
import configparser
import os
import sys

DEFAULT_PORT = 9198


def path():
  base = os.environ.get('APPDATA') or os.path.join(os.path.expanduser('~'), '.config')
  return os.path.join(base, 'rpmcli', 'rpmcli.ini')


def is_local(host):
  """True for this computer, which RPM lets connect without TLS."""
  return (host or '').lower() in ('', 'localhost', '127.0.0.1', '::1')


def interactive():
  """True when there is someone at a console to ask."""
  if sys.stdin is None or not sys.stdin.isatty():
    return False
  if os.name != 'nt':
    return True
  # isatty() is true for NUL too, a character device; only a console has a mode
  import ctypes, msvcrt
  mode = ctypes.c_ulong()
  try:
    handle = msvcrt.get_osfhandle(sys.stdin.fileno())
  except OSError:
    return False
  return ctypes.windll.kernel32.GetConsoleMode(ctypes.c_void_p(handle), ctypes.byref(mode)) != 0


def ask(prompt):
  """A line typed at the console, or None when input has ended."""
  try:
    return input(prompt).strip()
  except EOFError:
    return None


def _section(host, port):
  return '%s:%d' % (host.lower(), port)


def _read():
  cp = configparser.ConfigParser(interpolation = None)
  cp.read(path(), encoding = 'utf-8')
  return cp


def resolve(host = None, port = None, key = None, fingerprint = None):
  """Fill in whatever the caller left as None.

  Returns (host, port, key, fingerprint); key and fingerprint are '' when
  nothing supplies them.
  """
  cp = _read()
  host = host or os.environ.get('RPM_HOST') or cp.get('rpmcli', 'host', fallback = 'localhost')
  port = int(port or os.environ.get('RPM_PORT') or cp.get('rpmcli', 'port', fallback = DEFAULT_PORT))
  section = _section(host, port)
  if key is None:
    key = os.environ.get('RPM_KEY') or cp.get(section, 'key', fallback = '')
  if fingerprint is None:
    fingerprint = os.environ.get('RPM_FINGERPRINT') or cp.get(section, 'fingerprint', fallback = '')
  return host, port, key, fingerprint


def save(host, port, **values):
  """Store values (key, fingerprint) for host:port in the settings file."""
  cp = _read()
  section = _section(host, port)
  if not cp.has_section(section):
    cp.add_section(section)
  for name, value in values.items():
    cp.set(section, name, value)
  os.makedirs(os.path.dirname(path()), exist_ok = True)
  with open(path(), 'w', encoding = 'utf-8') as f:
    cp.write(f)
