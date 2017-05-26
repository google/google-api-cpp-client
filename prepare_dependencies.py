#!/usr/bin/python
# Copyright 2013 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Prepares dependencies for Google APIs Client Library for C++.

This *might* download, configure, build, and install the libraries we
depend on in whole or part. It is almost always not exactly you really
want, because each of the dependencies releases on their own cycle,
faster than this script will track them. For the most repeatable build
process, developers should manually pull the individual packages from
their respective repositories, inspect their licenses, add them to their
local revision control, and integrate into their build system.

Since this script is only marginally maintained, if it does not work for
you, your best options is to install the required components by hand.

Usage:
    By default, with no args, this will run turnkey doing whatever is needed.
    The provided options let you fine tune running specific packages. For
    example, if you need to upgrade a dependency or build again.
    To force a dependency to rebuild, use --force.

    [-b] Just build the dependent packages in the --download_dir
    [-d] Just download the dependent packages to the --download_dir
    [-i] Just install the dependencies to the --install_dir
    [--force] Ignore any previous results and force the request from scratch.
    [--download_dir=<path>] Specifies the download_dir.
                            The default path is ./external_dependencies.
    [--install_dir=<path>] Specifies the install_dir.
                            The default path is ./external_dependencies/install.
    [cmake|curl|gflags|glog|gmock]* Process just the specific subset.


    If you wish to obtain and build a newer (or older) version of
    a dependency, simply change the url in this file and run this
    script again on that name with the --force flag.
"""
import getopt
import glob
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib
import zipfile

COMPILED_MARKER = '_built'
INSTALLED_MARKER = '_installed'
CONFIGURED_MARKER = '_configured'

CYGWIN_PLATFORM = 'cygwin'
WINDOWS_PLATFORM = 'windows'
OSX_PLATFORM = 'osx'
LINUX_PLATFORM = 'Linux'

VS_COMPILER = 'VisualStudio'
GCC_COMPILER = 'gcc'

AUTO_CONFIG = 'auto'
CONFIGURE_CONFIG = 'configure'
CMAKE_CONFIG = 'cmake'


class ConfigInfo(object):
  """Configuration information for how to build the dependencies."""

  def __init__(self, abs_root_dir, unused_argv):
    """Initialize Configuration Information.

    Args:
      abs_root_dir: (sring) The path tot he build root directory.
      unused_argv: (string) The program arguments, including argv[0].
    """
    self._abs_root_dir = '%s' % abs_root_dir
    self._download_packages = False
    self._build_packages = False
    self._install_packages = False
    self._force = False
    self._debug = False
    self._download_dir = os.path.join(abs_root_dir, 'external_dependencies')
    self._abs_install_dir = '%s' % os.path.join(
        os.getcwd(), os.path.join('external_dependencies', 'install'))

    self._compiler = GCC_COMPILER
    if os.name == 'nt':
      self._port_name = WINDOWS_PLATFORM
      self._compiler = VS_COMPILER
    elif platform.system().startswith('CYGWIN'):
      self._port_name = CYGWIN_PLATFORM
    elif platform.system() == 'Darwin':
      self._port_name = OSX_PLATFORM
    elif platform.system() == 'Linux':
      self._port_name = LINUX_PLATFORM
    else:
      print 'Unknown system = %s. Assuming it is Linux compatible.' % (
          platform.system())
      self._port_name = LINUX_PLATFORM
    return

  def SetOptions(self, options):
    """Sets custom options.

    Args:
      options: (list[string, string])  name, value pairs of options given
    """
    do_all = True
    for opt, arg in options:
      if opt == '-b':
        do_all = False
        self._build_packages = True
      elif opt == '-d':
        do_all = False
        self._download_packages = True
      elif opt == '-i':
        do_all = False
        self._install_packages = True
      elif opt == '--force':
        self._force = True
      elif opt == '--download_dir':
        self._download_dir = arg
      elif opt == '--install_dir':
        if arg.startswith('/'):
          self._abs_install_dir = '%s' % arg
        else:
          self._abs_install_dir = os.path.join('%s' % os.getcwd(), arg)
      elif opt == '--debug':
        self._debug = True

    if do_all:
      self._build_packages = True
      self._download_packages = True
      self._install_packages = True

    if self._build_packages:
      print '   Build packages = True'
    if self._download_packages:
      print '   Download packages = True'
    if self._install_packages:
      print '   Installing packages = True'

    if self._download_packages:
      print '   Downloading files to %s' % self._download_dir
    if not os.path.exists(self._download_dir):
      os.makedirs(self._download_dir)

    if self._install_packages:
      print '   Installing packages to %s' % self._abs_install_dir
    if not os.path.exists(os.path.join(self._abs_install_dir, 'lib')):
      os.makedirs(os.path.join(self._abs_install_dir, 'lib'))
    if not os.path.exists(os.path.join(self._abs_install_dir, 'include')):
      os.makedirs(os.path.join(self._abs_install_dir, 'include'))

  @property
  def build_packages(self):
    """Returns whether we wwant to compile the packages."""
    return self._build_packages

  @property
  def download_packages(self):
    """Returns whether we wwant to download the packages."""
    return self._download_packages

  @property
  def install_packages(self):
    """Returns whether we wwant to install the packages."""
    return self._install_packages

  @property
  def compiler(self):
    """Returns the name of the compiler we prefer."""
    return self._compiler

  @property
  def port(self):
    """Returns the name of the platform we are preparing."""
    return self._port_name

  @property
  def make_command(self):
    """A tuple of (make_program_path, make_argument_list) for using Make."""
    if os.name == 'nt':
      program = 'nmake'
      args = '/C'
    else:
      program = 'make'
      args = ''

    return (program, args)

  @property
  def cmake_command(self):
    """A tuple of (cmake_program_path, cmake_argument_list) for using CMake."""
    if self._port_name == WINDOWS_PLATFORM:
      program = 'cmake'
      args = '-G "NMake Makefiles"'
    elif self._port_name == CYGWIN_PLATFORM:
      program = 'cmake'
      args = '-G "Unix Makefiles"'
    else:
      program = os.path.join(self._abs_install_dir, 'bin', 'cmake')
      args = ''
    if self._debug:
      args += ' -DCMAKE_BUILD_TYPE=Debug'
    return (program, args)

  @property
  def force(self):
    """Force all the work to be done again."""
    return self._force

  @property
  def download_dir(self):
    """The directory that we'll download and build external packages in."""
    return self._download_dir

  @property
  def abs_install_dir(self):
    """The root directory for the external dependency installation dir."""
    return self._abs_install_dir

  @property
  def abs_root_dir(self):
    """The root directory for the Google APIs for C++ sources."""
    return self._abs_root_dir


def _DownloadStatusHook(a, b, c):
  """Shows progress of download."""
  print '% 3.1f%% of %d bytes\r' % (min(100, float(a * b) / c * 100), c)


class PackageInstaller(object):
  """Acquires, builds, and installs an individual package for use in the SDK.
  """

  def __init__(self, config, url,
               make_target='all',
               package_name='',
               config_type=AUTO_CONFIG,
               extra_configure_flags=''):
    """Initializes for the individual package located at the given URL.

    Args:
      config: (Installer) The installer contains configuration info.
      url: (string) The url to install.
      make_target: (string) The make target to use when compiling.
      package_name: (string) Explicit package name if different than
                             indicated by the URL's archive.
      config_type: (string)  Specifies how this package is configured.
      extra_configure_flags: (string) Nonstandard flags for ./configure
    """
    self._config = config
    self._url = url
    self._config_type = config_type
    self._extra_configure_flags = extra_configure_flags
    self._extra_cppflags = ''
    self._extra_ldflags = ''
    self._make_target = make_target
    self._archive_file = os.path.split(url)[1]
    if not package_name:
      package_name = PackageInstaller._ArchiveToPackage(self._archive_file)

    self._package_name = package_name
    self._package_path = os.path.join(config.download_dir,
                                      self._package_name)
    self._vc_project_path = ''
    self._vc_upgrade_from_project_path = ''
    self._msbuild_args = ''

  def UpgradeVisualStudio(self, from_project, to_project):
    """Upgrades visual studio project.

    Args:
      from_project: (string) The path of the project to convert from.
      to_project: (string) The path of the project to convert to.
                           (conversion might be in-place).
    """
    if from_project == to_project or not os.path.exists(to_project):
      print '>>> Upgrading %s' % from_project
      upgrade_cmd = 'devenv "%s" /upgrade' % from_project
      PackageInstaller.RunOrDie(
          upgrade_cmd, 'Devenv failed to upgrade project.')

  def Download(self):
    """Downloads URL to file in configured download_dir."""
    config = self._config
    url = self._url
    filename = self._archive_file
    download_dir = config.download_dir
    download_path = os.path.join(download_dir, filename)
    if os.path.exists(download_path) and not config.force:
      print '%s already exists - skipping download from %s' % (filename, url)
      return

    print 'Downloading %s from %s: ' % (filename, url)
    try:
      urllib.urlretrieve(url, download_path, _DownloadStatusHook)
    except IOError:
      print ('\nERROR:\n'
             'Could not download %s.\n' % url
             + ('It could be that this particular version is no longer'
                ' available.\n'
                'Check the site where the url is coming from.\n'
                'If there is a more recent version then:\n'
                ' 1) Edit this script to change the old url to the new one.\n'
                ' 2) Run the script again.\n'
                '    It will pick up where it left off, using the new url.'
                '\n'))
      sys.exit(1)

  def MaybeTweakAfterUnpackage(self):
    """Extra stuff to do after unpackaging an archive."""
    config = self._config
    if config.compiler == VS_COMPILER and self._vc_upgrade_from_project_path:
      proj_dir = os.path.split(self._vc_project_path)[0]
      marker = os.path.join(proj_dir, '_upgraded_vc')
      if os.path.exists(self._vc_project_path) and config.force:
        if self._vc_upgrade_from_project_path != self._vc_project_path:
          os.unlink(self._vc_project_path)
        if os.path.exists(marker):
          os.unlink(marker)
      if (not os.path.exists(self._vc_project_path)
          or self._vc_project_path == self._vc_upgrade_from_project_path):
        self.UpgradeVisualStudio(self._vc_upgrade_from_project_path,
                                 self._vc_project_path)
        open(marker, 'w').close()

  def Unpackage(self):
    """Unpackages the archive into the package if needed."""
    config = self._config
    download_dir = config.download_dir
    archive_filename = self._archive_file
    package = self._package_name
    os.chdir(download_dir)

    if not os.path.exists(archive_filename):
      print '%s does not exist in %s' % (
          archive_filename, config.download_dir)
      sys.exit(1)

    if os.path.exists(self._package_name):
      if not config.force:
        self.MaybeTweakAfterUnpackage()
        return
      print 'Removing existing %s' % self._package_name
      shutil.rmtree(self._package_name)

    print '\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>'
    print '>>>  Unpacking %s into %s' % (archive_filename, package)
    if archive_filename.endswith('zip'):
      z = zipfile.ZipFile(archive_filename)
      for elem in z.namelist():
        dirname, filename = os.path.split(elem)
        if not os.path.exists(dirname):
          os.makedirs(dirname)
        if filename:
          with open(elem, 'w') as f:
            f.write(z.read(elem))
      z.close()
    elif archive_filename.endswith('.bz2'):
      try:
        subprocess.call('tar -xjf %s' % archive_filename, shell=True)
      except OSError:
        print 'Failed to unpack %s' % archive_filename
        sys.exit(-1)
    else:
      try:
        tar = tarfile.open(archive_filename)
        tar.extractall()
        tar.close()
      except IOError:
        try:
          subprocess.call('tar -xf %s' % archive_filename, shell=True)
        except OSError:
          print 'Failed to unpack %s' % archive_filename
          sys.exit(-1)

    self.MaybeTweakAfterUnpackage()
    print '>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>'

  def DetermineConfigType(self, path):
    """Detemines how to configure the directory depending on files.

    Args:
      path: (string) The path to be configured.
    Returns:
      The method to use for configuring the package.
    """
    if self._vc_project_path and self._config.compiler == VS_COMPILER:
      return None
    if os.path.exists(os.path.join(path, 'CMakeLists.txt')):
      return CMAKE_CONFIG
    if os.path.exists(os.path.join(path, 'configure')):
      return CONFIGURE_CONFIG
    if os.path.exists(os.path.join(path, 'Configure')):
      return CONFIGURE_CONFIG
    print 'Could not determine how to configure %s' % path
    sys.exit(1)

  def Configure(self):
    """Configure the package."""

    config = self._config
    os.chdir(self._package_path)
    marker_path = CONFIGURED_MARKER
    if os.path.exists(marker_path):
      if not config.force:
        print '%s already configured' % self._package_name
        return
      # remove built since we are forcing a rebuild
      os.unlink(marker_path)

    config_type = self._config_type
    if config_type == AUTO_CONFIG:
      config_type = self.DetermineConfigType('.')

    if not config_type:
      pass
    elif config_type == CMAKE_CONFIG:
      configure_cmd = '%s %s' % (
          config.cmake_command[0], config.cmake_command[1])
      prefix_arg = '-DCMAKE_INSTALL_PREFIX:PATH="%s" .' % (
          config.abs_install_dir)
    elif config_type == CONFIGURE_CONFIG:
      # Normally the automake uses a script called 'configure'
      # but for some reason openssl calls it 'Configure'.
      configure_cmd = os.path.join('.', 'configure')
      if not os.path.exists(configure_cmd):
        configure_cmd = os.path.join('.', 'Configure')

      prefix_arg = '--prefix="%s" %s' % (
          config.abs_install_dir, self._extra_configure_flags)

    if not config_type:
      cmd = None
    elif config.port == WINDOWS_PLATFORM:
      cmd = '%s %s' % (configure_cmd, prefix_arg)
    else:
      ldflags = '-L%s/lib %s' % (
          config.abs_install_dir, self._extra_ldflags)
      cppflags = '-I%s/include %s' % (
          config.abs_install_dir, self._extra_cppflags)
      cmd = 'LDFLAGS="%s" CPPFLAGS="%s" %s %s' % (
          ldflags, cppflags, configure_cmd, prefix_arg)

    if cmd:
      PackageInstaller.RunOrDie(
          cmd, 'Failed to configure %s' % self._package_name)

    # touch file so we know we configured it.
    open(marker_path, 'w').close()

    print '>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>'
    print '>>>  Finished configuring %s' % self._package_name
    print '>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n'

  def Compile(self):
    """Compiles a package but does not install it."""
    package_name = self._package_name
    config = self._config
    os.chdir(self._package_path)
    marker_path = COMPILED_MARKER
    if os.path.exists(marker_path):
      if not config.force:
        print '%s already built' % package_name
        return
      # remove built since we are forcing a rebuild
      os.unlink(marker_path)

    PackageInstaller._VerifyMakeOrDie(config.make_command[0])
    print '++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++'
    print '+++  Building %s [%s]' % (package_name, self._make_target)
    print '++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++'
    if self._vc_project_path and config.compiler == VS_COMPILER:
      dirname, filename = os.path.split(self._vc_project_path)
      os.chdir(dirname)
      make_cmd = 'msbuild "%s" %s' % (filename, self._msbuild_args)
    else:
      make_cmd = '%s %s %s' % (
          config.make_command[0], config.make_command[1], self._make_target)
    PackageInstaller.RunOrDie(make_cmd, 'Failed to make %s' % package_name)

    # touch file so we know we built it.
    open(marker_path, 'w').close()

    print '++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++'
    print '+++  Finished building %s' % package_name
    print '++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n'

  def Install(self):
    """Installs a pre-built package using a make-rule."""
    self.MakeInstall()

  def MakeInstall(self):
    """Installs a pre-built package, including ones we built ourself."""
    package_name = self._package_name
    config = self._config
    os.chdir(self._package_path)
    marker_path = INSTALLED_MARKER
    if os.path.exists(marker_path):
      if not config.force:
        print '%s already installed' % package_name
        return
      # remove built since we are forcing an install
      os.unlink(marker_path)

    PackageInstaller._VerifyMakeOrDie(config.make_command[0])

    print '\n++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++'
    print '+++  Installing %s' % package_name
    print '++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++'

    cmd = '%s %s install' % config.make_command
    PackageInstaller.RunOrDie(cmd, 'Failed to install %s' % package_name)

    # touch file so we know we installed it.
    open(marker_path, 'w').close()

    print '++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++'
    print '+++  Finished installing %s' % package_name
    print '++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++'

  def Process(self):
    """Runs standard workflow to obatin and prepare dependencies."""
    config = self._config
    if config.download_packages:
      self.Download()

    if config.build_packages or config.install_packages:
      self.Unpackage()

    if config.build_packages or config.install_packages:
      self.Configure()

    if config.build_packages:
      self.Compile()

    if config.install_packages:
      self.Install()

  @classmethod
  def CopyAllFiles(cls, from_dir, to_dir):
    """Copies one directory tree into another.

    Args:
      from_dir: (string) The directory to copy from.
      to_dir: (string) The directory to copy to.
    """
    for elem in glob.glob(os.path.join(from_dir, '*')):
      tail = os.path.split(elem)[1]
      if os.path.isdir(elem):
        targetdir = os.path.join(to_dir, tail)
        if not os.path.exists(targetdir):
          os.makedirs(targetdir)
        PackageInstaller.CopyAllFiles(elem, targetdir)
      else:
        targetfile = os.path.join(to_dir, tail)
        shutil.copyfile(elem, targetfile)
        shutil.copystat(elem, targetfile)

  @classmethod
  def RunOrDie(cls, cmd, error_msg):
    """Runs system command. Dies if the command fails.

    Args:
      cmd: (string) Command to execute.
      error_msg: (string) Optional additional error to print on failure.
    """
    try:
      print '>>> Executing [%s] in %s' % (cmd, os.getcwd())
      ok = os.system(cmd) == 0
    except OSError:
      ok = False

    if not ok:
      print 'Failed command: [%s] in %s' % (cmd, os.getcwd())
      if error_msg:
        print '   %s' % error_msg
      sys.exit(1)

  @classmethod
  def _ArchiveToPackage(cls, archive):
    """Return the package name for a given archive.

    Usually this is the archive name stripped of the .tar.gz or .zip suffix.

    Args:
      archive: (string) A tar or zip file.
    Returns:
      The package name of the archive strips the .tar.gz or .zip extension.
    """
    for suffix in ['.tar.gz', '.zip', '.tgz', '.tar.bz2']:
      if archive.endswith(suffix):
        return archive[0:len(archive) - len(suffix)]

    print 'Unhandled archive=%s' % archive
    sys.exit(1)

  @classmethod
  def _VerifyCMakeOrDie(cls, cmake_program):
    """Verifies that CMake is on the path or exits.

    Args:
      cmake_program: (string) The cmake program to check for.
    """
    if not cls._VerifyProgram(cmake_program, '--version'):
      print('Could not find "cmake" on your PATH. '
            'Try running this script again with the arguments '
            '"-di cmake".\n'
            'Then try building and installing again.')
      exit(1)

  @classmethod
  def _VerifyMakeOrDie(cls, make_program):
    """Verifies that the make_program variable is on the path or exits.

    Args:
      make_program: (string) The make program to check for.
    """
    install_instructions = ''
    args = '--version'
    if os.name == 'nt':
      args = '/C /HELP'
      install_instructions = (
          'If you are using Visual Studio, try running the vcvars.bat script '
          ' for the version of Visual Studio you wish to use. '
          ' For 64-bit Visual Studio 11.0 this is something like '
          ' C:\\"Program Files (x86)"\\"Microsoft Visual Studio 11.0"'
          '\\VC\\bin\\x86_amd64\\vcvars64.bat')

    if not cls._VerifyProgram(make_program, args):
      print('Make sure that "%s" is in your path. %s' % (
          make_program, install_instructions))
      exit(1)

  @classmethod
  def _VerifyProgram(cls, prog, args):
    """Verify the program exists on the path.

    Args:
      prog: (string) The name of the program to check.
      args: (string) args to give program when running it.

    Returns:
      True if program is on PATH, False otherwise.
    """
    try:
      test = '%s %s' % (prog, args)
      subprocess.Popen(test, stdout=subprocess.PIPE, shell=True)
    except OSError:
      return False
    return True


class MongoosePackageInstaller(PackageInstaller):
  """Custom installer for the Mongoose package."""

  def __init__(self, config, url, package_path='mongoose'):
    """Standard PackageInstaller initializer."""
    super(MongoosePackageInstaller, self).__init__(config, url)
    self._config_type = CMAKE_CONFIG
    self._package_path = os.path.join(self._config.download_dir, package_path)

  def MaybeTweakAfterUnpackage(self):
    """Creates a CMakeLists.txt file for building the package."""
    config = self._config
    cmakelists_path = os.path.join(self._package_path, 'CMakeLists.txt')
    if config.force and os.path.exists(cmakelists_path):
      os.unlink(cmakelists_path)
    if os.path.exists(cmakelists_path):
      return

    # Mongoose just builds a server, and does so nonstandard.
    # We want a library. There's only one file so pretty simple.
    print '>>> Creating CMakeLists.txt as %s' % cmakelists_path
    with open(cmakelists_path, 'w') as f:
      f.write('cmake_minimum_required (VERSION 2.6)\n')
      f.write('project (Mongoose)\n')
      f.write('add_library(mongoose STATIC mongoose.c)\n')
      f.write('add_definitions( -DNO_CGI -U__STDC_FORMAT_MACROS )\n')

  def Install(self):
    """Copies headers and libraries to install the package."""
    config = self._config
    include_dir = os.path.join(config.abs_install_dir, 'include', 'mongoose')
    if not os.path.exists(include_dir):
      os.makedirs(include_dir)
    shutil.copy(os.path.join(self._package_path, 'mongoose.h'), include_dir)

    libdir = os.path.join(config.abs_install_dir, 'lib')
    if not os.path.exists(libdir):
      os.makedirs(libdir)

    if config.port != WINDOWS_PLATFORM:
      shutil.copy(os.path.join(self._package_path, 'libmongoose.a'), libdir)
    else:
      for ext in ['lib', 'pdb']:
        shutil.copy(os.path.join(self._package_path, 'mongoose.%s' % ext),
                    libdir)


class JsonCppPackageInstaller(PackageInstaller):
  """Custom installer for the JsonCpp package."""

  def __init__(self, config, url):
    """Standard PackageInstaller initializer."""
    super(JsonCppPackageInstaller, self).__init__(config, url)
    self._config_type = CMAKE_CONFIG

  def MaybeTweakAfterUnpackage(self):
    """Creates a CMakeLists.txt to build the package."""
    config = self._config
    os.chdir(self._package_path)
    if config.force and os.path.exists('CMakeLists.txt'):
      os.unlink('CMakeLists.txt')
    if os.path.exists('CMakeLists.txt'):
      return

    if config.build_packages:
      allfiles = ''

    src_path = os.path.join('src', 'lib_json')
    for elem in glob.glob('%s/*.cpp' % src_path):
      allfiles = '%s "%s"' % (allfiles, elem)

    print '>>>  Creating CMakeLists.txt'
    with open('CMakeLists.txt', 'w') as f:
      f.write('cmake_minimum_required (VERSION 2.6)\n')
      f.write('project (JsonCpp)\n')
      f.write('INCLUDE_DIRECTORIES(./include src/lib_json)\n')
      f.write('add_library(jsoncpp STATIC %s)\n' % allfiles)

  def Install(self):
    """Copies the libraries nad header files to install the package."""
    config = self._config
    print '>>>  Installing %s' % self._package_name
    PackageInstaller.CopyAllFiles(
        os.path.join(self._package_path, 'include'),
        os.path.join(config.abs_install_dir, 'include'))
    libdir = os.path.join(config.abs_install_dir, 'lib')
    if not os.path.exists(libdir):
      os.makedirs(libdir)

    if config.port != WINDOWS_PLATFORM:
      shutil.copy('libjsoncpp.a', libdir)
    else:
      for ext in ['lib', 'pdb']:
        shutil.copy('jsoncpp.%s' % ext, libdir)


class CMakeExeInstaller(PackageInstaller):
  """Installs CMake under Windows from initialization executable."""

  def __init__(self, config, url):
    """Standard PackageInstaller initializer."""
    super(CMakeExeInstaller, self).__init__(config, url, package_name='ignore')

  def Unpackage(self):
    return

  def Configure(self):
    return

  def Compile(self):
    return

  def Install(self):
    """Runs installer to install CMake (on the system)."""
    config = self._config
    if not config.force:
      if PackageInstaller._VerifyProgram('cmake', '--version'):
        print 'Already have CMake'
        return

    print 'Installing CMake'
    download_dir = config.download_dir
    exe_filename = self._archive_file
    os.chdir(download_dir)
    # This is a self-installing .exe file
    PackageInstaller.RunOrDie(
        exe_filename, 'Failed to install CMake from %s.' % exe_filename)


class IgnorePackageInstaller(PackageInstaller):
  """This package initializer does nothing."""

  def __init__(self, config, url):
    """Standard PackageInstaller initializer."""
    super(IgnorePackageInstaller, self).__init__(
        config, url, package_name='ignore')

  def Download(self):
    return

  def Unpackage(self):
    return

  def Configure(self):
    return

  def Compile(self):
    return

  def Install(self):
    return


class OpenSslPackageInstaller(PackageInstaller):
  """Custom installer for the OpenSsl package."""

  def __init__(self, config, url):
    """Standard PackageInstaller initializer."""
    super(OpenSslPackageInstaller, self).__init__(config, url)
    if config.port == OSX_PLATFORM:
      self._extra_configure_flags = 'darwin64-x86_64-cc'
    elif platform.system() == 'Linux':
      self._extra_configure_flags = 'linux-%s' % platform.machine()
    else:
      self._extra_configure_flags = 'gcc'

  def Configure(self):
    # TODO(user): 20130626
    # These artifacts are probably not even needed. Investigate for a
    # future release.
    print 'NOTE for Google APIs Client Library for C++ Installer:'
    print '  If this fails it might be because we guessed the wrong platform.'
    print '  Edit prepare_dependencies.py and notify us.'
    print '  See the README in the release for contact information.'
    super(OpenSslPackageInstaller, self).Configure()


class GFlagsPackageInstaller(PackageInstaller):
  """Custom installer for the GFlags package."""

  def __init__(self, config, url, package_name=None):
    """Standard PackageInstaller initializer."""
    super(GFlagsPackageInstaller, self).__init__(config, url,
                                                 package_name=package_name)
    self._archive_file = self._archive_file.replace('-no-svn-files', '')
    self._package_name = self._package_name.replace('-no-svn-files', '')
    self._package_path = self._package_path.replace('-no-svn-files', '')
    self._msbuild_args = '/p:Configuration=Release;Platform=x86'
    self._vc_upgrade_from_project_path = (
        '%s\\vsprojects\\libgflags\\libgflags.vcproj' % self._package_path)
    self._vc_project_path = self._vc_upgrade_from_project_path.replace(
        '.vcproj', '.vcxproj')

  def Install(self):
    """Copis generated libs and headers into the install directory."""
    config = self._config
    if config.compiler != VS_COMPILER:
      super(GFlagsPackageInstaller, self).Install()
      return
    print '>>>  Installing %s' % self._package_name
    install_libdir = os.path.join(config.abs_install_dir, 'lib')
    install_includedir = os.path.join(config.abs_install_dir,
                                      'include', 'gflags')
    if not os.path.exists(install_libdir):
      os.makedirs(install_libdir)
    if not os.path.exists(install_includedir):
      os.makedirs(install_includedir)
    PackageInstaller.CopyAllFiles(
        os.path.join(self._package_path, 'src', 'windows', 'gflags'),
        install_includedir)
    release_dir = os.path.join(self._package_path,
                               'vsprojects', 'libgflags', 'Release')
    for ext in ['lib', 'dll', 'pdb']:
      print 'renaming %s.%s' % (os.path.join(release_dir, 'libgflags'), ext)
      shutil.copyfile(
          '%s.%s' % (os.path.join(release_dir, 'libgflags'), ext),
          '%s.%s' % (os.path.join(install_libdir, 'libgflags'), ext))


class GMockPackageInstaller(PackageInstaller):
  """Custom installer for the GMock package."""

  def __init__(self, config, url, package_name=None):
    """Standard PackageInstaller initializer.

    Args:
      config: (ConfigInfo) Configuration information.
      url: (string)  The URL to download from.
    """
    super(GMockPackageInstaller, self).__init__(config, url,
                                                package_name=package_name)

  def MaybeTweakAfterUnpackage(self):
    if self._config.compiler == VS_COMPILER:
      # But this wont actually build in visual studio because VC11 changed the
      # default number of variadic template parameters from 10 to 5 and we
      # need 10. So patch the build flags to force 10
      cmake_utils_path = os.path.join(
          self._package_path, 'gtest', 'cmake', 'internal_utils.cmake')
      with open(cmake_utils_path, 'r') as f:
        text = f.read()
      inject_flags = '-D_VARIADIC_MAX=10'
      if text.find(inject_flags) < 0:
        insert_after = '-DSTRICT -DWIN32_LEAN_AND_MEAN'
        text = text.replace(insert_after, '%s %s' % (
            insert_after, inject_flags))
        with open(cmake_utils_path, 'w') as f:
          f.write(text)

  def Configure(self):
    return

  def Compile(self):
    return

  def Install(self):
    # GMock needs to be installed in the build tree (i.e. the root_dir/src
    # because that's what it wants.
    gmock_path = os.path.join(self._config.abs_root_dir, 'src', 'gmock')

    if self._config.force and os.path.exists(gmock_path):
      shutil.rmtree(gmock_path)
    if os.path.exists(gmock_path):
      return
    shutil.copytree(self._package_path, gmock_path)


class GLogPackageInstaller(PackageInstaller):
  """Custom installer for the GLog package."""

  def __init__(self, config, url, package_name=None):
    """Standard PackageInstaller initializer.

    Args:
      config: (ConfigInfo) Configuration information.
      url: (string)  The URL to download from.
    """
    super(GLogPackageInstaller, self).__init__(config, url,
                                               package_name=package_name)
    self._msbuild_args = '/p:Configuration=Release;Platform=x86'
    self._vc_upgrade_from_project_path = (
        '%s\\vsprojects\\libglog\\libglog.vcproj' % self._package_path)
    self._vc_project_path = (
        '%s\\vsprojects\\libglog\\libglog.vcxproj' % self._package_path)

  def MaybeTweakAfterUnpackage(self):
    """Tweaks a header file declaration under windows so it compiles."""
    super(GLogPackageInstaller, self).MaybeTweakAfterUnpackage()

    remove_cygwin_paths = [
        os.path.join(self._package_path, 'src', 'googletest.h'),
        os.path.join(self._package_path, 'src', 'utilities.cc')
    ]
    for change_path in remove_cygwin_paths:
      changed = False
      with open(change_path, 'r') as f:
        old_text = f.read()
        # The source couple windows and cygwin together for some reason,
        # but that doesnt compile. CYGWIN appears to work if you take these
        # out (it will use pthreads instead of the windows API).
        text = old_text.replace('defined(OS_WINDOWS) || defined(OS_CYGWIN)',
                                'defined(OS_WINDOWS)')
        text = text.replace('defined OS_WINDOWS || defined OS_CYGWIN',
                            'defined(OS_WINDOWS)')
        changed = old_text != text
      if changed:
        with open(change_path, 'w') as f:
          f.write(text)
        print 'Hacked %s' % change_path

    logging_h_path = os.path.join(
        self._package_path, 'src', 'windows', 'glog', 'logging.h')
    changed = False
    with open(logging_h_path, 'r') as f:
      old_text = f.read()
      text = old_text.replace('class LogStreamBuf',
                              'class GOOGLE_GLOG_DLL_DECL LogStreamBuf')
      changed = old_text != text

    if changed:
      with open(logging_h_path, 'w') as f:
        f.write(text)
      print 'Hacked %s' % logging_h_path

  def Install(self):
    """Overrides install to copy the generated headers and libs."""
    if self._config.port != WINDOWS_PLATFORM:
      super(GLogPackageInstaller, self).Install()
      return

    config = self._config
    install_libdir = os.path.join(config.abs_install_dir, 'lib')
    install_includedir = os.path.join(
        config.abs_install_dir, 'include', 'glog')
    print '>>>  Installing %s' % self._package_name
    if not os.path.exists(install_libdir):
      os.makedirs(install_libdir)
    if not os.path.exists(install_includedir):
      os.makedirs(install_includedir)
    PackageInstaller.CopyAllFiles(
        os.path.join(self._package_path, 'src', 'windows', 'glog'),
        install_includedir)
    release_dir = os.path.join(
        self._package_path, 'vsprojects', 'libglog', 'Release')
    for ext in ['lib', 'dll', 'pdb']:
      shutil.copyfile('%s.%s' % (os.path.join(release_dir, 'libglog'), ext),
                      '%s.%s' % (os.path.join(install_libdir, 'libglog'), ext))


class CurlPackageInstaller(PackageInstaller):

  def __init__(self, config, url):
    if config.compiler == VS_COMPILER:
      config_type = CMAKE_CONFIG
    else:
      config_type = CONFIGURE_CONFIG
    super(CurlPackageInstaller, self).__init__(
        config, url, config_type=config_type)


class Installer(object):
  """Acquires, builds, and installs dependencies for the SDK."""

  def __init__(self, config, restricted_package_names):
    """Set up variables from flags. Exit on failure.

    Args:
      config: (ConfigInfo) Configuraton options.
      restricted_package_names: (Array) Subset of package names to install.
    """

    print 'Initializing....'
    # If non-empty then just prepare these packages.
    # The entries here are keys in url_map
    self._restricted_packages = restricted_package_names

    self._url_map = {}
    if config.port == WINDOWS_PLATFORM or config.port == CYGWIN_PLATFORM:
      self._url_map.update({
          # Use CMake as our build system for the libraries and some deps
          'cmake': (CMakeExeInstaller(
              config,
              'http://www.cmake.org/files/v3.1/cmake-3.1.1-win32-x86.exe')),

          'openssl': (IgnorePackageInstaller(config, 'ignoring_openssl')),
      })
    else:
      self._url_map.update({
          # Use CMake as our build system for the libraries and some deps
          'cmake': (PackageInstaller(
              config,
              'http://www.cmake.org/files/v3.1/cmake-3.1.1.tar.gz',
              config_type=CONFIGURE_CONFIG)),

          # This is used both for curl https support and
          # the OpenSslCodec library for the OpenSslCodec for data encryption.
          # The OpenSslCodec is not requird so if you get an https transport
          # from somewhere else then you do not need this dependency.
          'openssl': (OpenSslPackageInstaller(
              config, 'https://www.openssl.org/source/openssl-1.1.0e.tar.gz')),
          })

    self._url_map.update({
        # GFlags is only used for some examples.
        # Only used for tests and samples.
        'gflags': (GFlagsPackageInstaller(
          config,
          'https://github.com/gflags/gflags/archive/v2.2.0.tar.gz',
          'gflags-2.2.0')),

        # GLog is the logging mechanism used through the client API
        'glog': (GLogPackageInstaller(
          config,
          'https://github.com/google/glog/archive/v0.3.4.tar.gz',
          'glog-0.3.4')),

        # GMock (and included GTest) are only used for tests, not runtime
        # Only used for tests.
        'gmock': (GMockPackageInstaller(
          config,
          'https://github.com/google/googlemock/archive/release-1.7.0.tar.gz',
          'googlemock-release-1.7.0')),

        # For now we use JsonCpp for JSON support in the Client Service Layer
        # and other places where we process JSON encoded data.
        'jsoncpp': (JsonCppPackageInstaller(
            config,
            'http://downloads.sourceforge.net/project/jsoncpp'
            '/jsoncpp/0.5.0/jsoncpp-src-0.5.0.tar.gz')),

        # Mongoose is used as webserver for samples.
        # The ownership and license style seems to keep changing, so we do not
        # download it by default.
        # 'mongoose': (MongoosePackageInstaller(
        #   config,
        #   'https://github.com/cesanta/mongoose/archive/6.7.zip',
        #   'mongoose-6.7')),

        'curl': (CurlPackageInstaller(
            config, 'https://github.com/curl/curl/releases/download/curl-7_54_0/curl-7.54.0.tar.gz')),
        })

    # make sure cmake occurs first since others may depend on it
    if not self._restricted_packages:
      self._restricted_packages = self._url_map.keys()
      ordered_packages = ['cmake', 'openssl', 'glog']
      for p in ordered_packages:
        self._restricted_packages.remove(p)
      self._restricted_packages = ordered_packages + self._restricted_packages

  def ProcessDependencyOrDie(self, name):
    """Process a single dependency.

    Args:
      name: (string) The name of the dependency to process.
    """

    value = self._url_map.get(name)
    if not value:
      print 'Unknown package "%s"' % name
      sys.exit(1)
    value.Process()

  def Run(self):
    """Run the installer.

    Returns:
      Packages that were processed.
    """
    restricts = self._restricted_packages

    # Attempt to process each of the requested packages.
    for key in restricts:
      self.ProcessDependencyOrDie(key)
    return restricts


if __name__ == '__main__':
  config_info = ConfigInfo(os.getcwd(), sys.argv)
  restricted_packages = []
  try:
    opts, restricted_packages = getopt.getopt(
        sys.argv[1:], 'bdi', ['download_dir=', 'install_dir=', 'force',
                              'debug'])
    config_info.SetOptions(opts)
  except getopt.GetoptError:
    print ('%s: [-b] [-d] [-i]' % sys.argv[0]
           + '[--download_dir=<path>] [--install_dir=<path>] [--force]')
    sys.exit(1)

  installer = Installer(config_info, restricted_packages)

  processed_packages = installer.Run()
  print '\nFinished processing %s' % str(processed_packages)
