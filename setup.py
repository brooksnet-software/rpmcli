"""
    setupTools based setup script for
 
    To install: python setup.py install
    
"""

from setuptools import setup

version = '0.1.0'

PACKAGES = ['rpmcli', 'rpmcli.src', 'rpmcli.src.backend']
            

setup(name = 'rpmcli',
      version = version,
      description = "RPM Remote Print Manager CLI",
      author="Daniel Casper, Brooks Internet Software",
      author_email="daniel@brooksnet.com",
      url="https://github.com/gddc/rpmcli",
      classifiers=["Programming Language :: Python",
                   "Intended Audience :: Developers",
                   "Topic :: Office/Business"],
      keywords="RPM Remote Print Manager Command Line Interface CLI",
      packages=PACKAGES,
      python_requires='>=3.8',
      install_requires=[]
)