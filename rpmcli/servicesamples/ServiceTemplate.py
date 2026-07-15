'''
Created on Mar 3, 2011

@author: daniel
'''
import win32serviceutil
import win32event
import sys

class Launcher(win32serviceutil.ServiceFramework):
  _svc_name_ = "<insert service name>"

  _svc_display_name_ = "<insert display name>"

  def __init__(self, args):
    win32serviceutil.ServiceFramework.__init__(self, args)
    self.hWaitStop = win32event.CreateEvent(None, 0, 0, None)

  def SvcStop(self):
    sys.stopservice = True

  def SvcDoRun(self):
    # Call a Main() like function here, ideally something that will
    # know to close when the `sys` module gains a 'stopservice' attribute.
    pass


if __name__ == '__main__':
  # py2exe provided a dedicated `service` build target; PyInstaller does not,
  # so the frozen executable hosts and controls the service itself.
  import servicemanager
  if len(sys.argv) == 1:
    # Started by the Windows Service Control Manager (no arguments).
    servicemanager.Initialize()
    servicemanager.PrepareToHostSingle(Launcher)
    servicemanager.StartServiceCtrlDispatcher()
  else:
    # Command-line control: install / start / stop / remove.
    win32serviceutil.HandleCommandLine(Launcher)
