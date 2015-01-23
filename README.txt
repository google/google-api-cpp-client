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

About the branches

  The master branch is were development is done and usually matches the
  generated libraries available from from google.developers.com. For breaking
  changes in this SDK (when it gets ahead of the library generator), we will
  first make a tag matching the compatible version of the library generator.

  For example, the current generated surface is at version '0.1.2'. If the
  master branch gets incompatibly ahead of the generator, we will first leave
  a '0.1.2' tag here to retrieve the compatible version.

  We use tags rather than branches because the library generator should soon
  catch up. All fixes will be done in the master branch, so a tag is
  sufficient.

  The latest generated libraries for any Google API is available automatically
  from
  https://developers.google.com/resources/api-libraries/download/<API>/<VERSION>/cpp

  For example, for Drive/v2, you would use
  https://developers.google.com/resources/api-libraries/download/drive/v2/cpp
