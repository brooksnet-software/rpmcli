#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Build a Windows service executable from ServiceTemplate.py using PyInstaller.
This replaces the former py2exe service build script (py2exesetup.py).

py2exe had a dedicated ``service`` build target; PyInstaller does not.  The
equivalent modern approach is:

  1. Give the service module a ``__main__`` entry point that dispatches to the
     Service Control Manager or to win32serviceutil.HandleCommandLine
     (already done in ServiceTemplate.py).
  2. Freeze that module into an ordinary one-file console executable.
  3. Install and control the resulting executable via its own command line::

         ServiceTemplate.exe install
         ServiceTemplate.exe start
         ServiceTemplate.exe stop
         ServiceTemplate.exe remove

Usage:
    python build_pyinstaller.py

Requires:
    pip install pyinstaller pywin32
"""

import os
from platform import architecture

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(HERE, '..', 'dist_%s' % architecture()[0])
WORK = os.path.join(HERE, 'build')

SERVICE_SCRIPT = os.path.join(HERE, 'ServiceTemplate.py')


def build():
    import PyInstaller.__main__
    PyInstaller.__main__.run([
        SERVICE_SCRIPT,
        '--onefile',
        '--console',
        '--noconfirm',
        '--clean',
        '--distpath', DIST,
        '--workpath', WORK,
        '--specpath', WORK,
        # pywin32 service plumbing that PyInstaller can't detect on its own.
        '--hidden-import', 'win32timezone',
        '--hidden-import', 'servicemanager',
        '--exclude-module', 'tkinter',
    ])


if __name__ == '__main__':
    print('Building service executable into %s' % DIST)
    build()
