#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Build standalone console executables for the RPM CLI utilities using
PyInstaller.  This replaces the former py2exe build script (py2exesetup.py);
py2exe no longer supports current Python, and distutils (which it relied on)
was removed in Python 3.12.

Each ``*.py`` utility in this directory -- except this build script and
``__init__.py`` -- is frozen into its own one-file console executable under
``../standalones_<arch>/``.

Usage:
    python build_pyinstaller.py

Requires:
    pip install pyinstaller
"""

import os
from glob import glob
from platform import architecture

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))  # parent of rpmcli/ -- so `import rpmcli` resolves
DIST = os.path.join(HERE, '..', 'standalones_%s' % architecture()[0])
WORK = os.path.join(HERE, 'build')

# Never build these into executables.
EXCLUDE = {'__init__.py', os.path.basename(__file__)}


def scripts():
    """Return the utility scripts in this directory that should be frozen."""
    found = {os.path.basename(p) for p in glob(os.path.join(HERE, '*.py'))}
    return sorted(os.path.join(HERE, name) for name in found - EXCLUDE)


def build(script):
    import PyInstaller.__main__
    PyInstaller.__main__.run([
        script,
        '--onefile',
        '--console',
        '--noconfirm',
        '--clean',
        '--distpath', DIST,
        '--workpath', WORK,
        '--specpath', WORK,
        '--paths', ROOT,              # resolve `import rpmcli...`
        '--exclude-module', 'tkinter',
    ])


if __name__ == '__main__':
    targets = scripts()
    print('Building %d executable(s) into %s' % (len(targets), DIST))
    for script in targets:
        print('  - %s' % os.path.basename(script))
        build(script)
