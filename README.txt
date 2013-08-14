For more information regarding installation, consult the following document:
  http://google.github.io/google-api-cpp-client/latest/start/installation.html

To get started using the Google APIs Client Library for C++ see:
  http://google.github.io/google-api-cpp-client/latest/start/get_startedhtml

The Doxygen-generated API reference is available online at:
  http://google.github.io/google-api-cpp-client/latest/doxygen/html/index.html

For the current Google APIs Client Library for C++ see:
  http://github.com/google/google-api-cpp-client/

The current installation has only been tested on Unix/Linux systems including OS X.
This release does not support Windows yet. The following sequence of
actions should result in a turnkey build of the client libraries from
the source code given only:
  Prerequisites:
    * python   (Available from http://www.python.org/getit/)
        - verified with versions 2.6.4 and 2.7.3
    * C++ compiler and Make
        - Mac OS X https://developer.apple.com/xcode/
        - Linux   http://gcc.gnu.org/
    * CMake
        - Either http://www.cmake.org/cmake/resources/software.html
        - or run ./prepare_dependencies.py cmake
          and restart your shell to get the updated path.

  Bootstrap Steps:
    ./prepare_dependencies.py
    ./external_dependencies/install/bin/cmake .
    make all
    make test
    make export

To download additional APIs specialized for individual Google Services see:
http://google.github.io/google-api-cpp-client/latest/available_service_apis.html

It should be possible to build this from existing installed libraries.
However, the build scripts are not yet written to find them. For initial
support simplicity we download and build all the dependencies in the
prepare_dependencies.py script for the time being as a one-time brute
force preparation.

If you have problems, questions, suggestions, or accoldtes, contact:
  The Google group at https://groups.google.com/group/google-api-cpp-client

Or you may also ask questions on StackOverflow at:
   http://stackoverflow.com with the tag google-api-cpp-client




