For more information regarding installation, consult the following document:
  http://google.github.io/google-api-cpp-client/latest/start/installation.html

To get started using the Google APIs Client Library for C++ see:
  http://google.github.io/google-api-cpp-client/latest/start/get_started.html

The Doxygen-generated API reference is available online at:
  http://google.github.io/google-api-cpp-client/latest/doxygen/index.html

For the current Google APIs Client Library for C++ see:
  http://github.com/google/google-api-cpp-client/

The current installation has only been tested on Unix/Linux systems and OS/X.
This release does not support Windows yet. The following sequence of
actions should result in a turnkey build of the client libraries from
the source code given only:
  Prerequisites:
    * python   (Available from http://www.python.org/getit/)
        - verified with versions 2.6.4 and 2.7.3
    * C++ compiler and Make
        - Mac OSX https://developer.apple.com/xcode/
        - Linux   http://gcc.gnu.org/
    * CMake
        - Either http://www.cmake.org/cmake/resources/software.html
        - or run ./prepare_dependencies.py cmake
          and restart your shell to get the updated path.

  Bootstrap Steps:
    ./prepare_dependencies.py
    mkdir build && cd build
    ../external_dependencies/install/bin/cmake ..
    make all
    make test
    make install

To download additional APIs specialized for individual Google Services see:
http://google.github.io/google-api-cpp-client/latest/available_service_apis.html

It should be possible to build this from existing installed libraries.
However, the build scripts are not yet written to find them. For initial
support simplicity we download and build all the dependencies in the
prepare_dependencies.py script for the time being as a one-time brute
force preparation.

If you have problems, questions or suggestions, contact:
  The Google group at https://groups.google.com/group/google-api-cpp-client

Or you may also ask questions on StackOverflow at:
   http://stackoverflow.com with the tag google-api-cpp-client

Status

  This SDK is in maintanance mode. The patches being made are mostly for
  portability and/or to remove unneeded pieces.

  We are not set up to accept pull requests at this time, nor will be in the
  forseable future. Please submit suggestions as issues.

About the branches

  The master branch is where development is done. It usually is compatible with
  the generated libraries available from from google.developers.com. On occasion
  it gets aheaad of those. It usually catches up in a few days.

  The latest generated libraries for any Google API is available automatically
  from
  https://developers.google.com/resources/api-libraries/download/<API>/<VERSION>/cpp

  For example, for Drive/v2, you would use
  https://developers.google.com/resources/api-libraries/download/drive/v2/cpp
