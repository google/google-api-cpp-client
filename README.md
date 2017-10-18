# Google API C++ Client

The current installation has only been tested on Unix/Linux systems;
this release does not support Windows yet. The following sequence of
actions should result in a turnkey build of the client libraries from
the source code given only:

## Building

### Prerequisites:
* python   (Available from http://www.python.org/getit/)
  - verified with versions 2.6.4 and 2.7.3
* C++ compiler and Make
  - Mac OSX https://developer.apple.com/xcode/
  - Linux   http://gcc.gnu.org/

### Build Steps:

    ./prepare_dependencies.py
    mkdir build && cd build
    ../external_dependencies/install/bin/cmake ..
    make

## Running the Samples

See [src/samples/README.md](src/samples/README.md)


## Building Clients for Other APIs

To download additional APIs specialized for individual Google Services see:
http://google.github.io/google-api-cpp-client/latest/available_service_apis.html
and use this precise version of the apis client generator:
https://github.com/google/apis-client-generator/tree/dcad06f5ff0fecfcf7a029efefe62a6b6287b025

Here's an example invocation:

    $ python apis-client-generator/src/googleapis/codegen/generate_library.py --api_name=drive --api_version=v2 --language=cpp --output_dir=/tmp/generated

It should be possible to build this from existing installed libraries.
However, the build scripts are not yet written to find them. For initial
support simplicity we download and build all the dependencies in the
prepare_dependencies.py script for the time being as a one-time brute
force preparation.

### Getting Help

If you have problems, questions or suggestions, contact:
  The Google group at https://groups.google.com/group/google-api-cpp-client

Or you may also ask questions on StackOverflow at:
   http://stackoverflow.com with the tag google-api-cpp-client

## Status

  This SDK is in maintanance mode. The patches being made are mostly for
  portability and/or to remove unneeded pieces.

  We are not set up to accept pull requests at this time, nor will be in the
  forseable future. Please submit suggestions as issues.

### About the branches

  The master branch is where development is done. It usually is compatible with
  the generated libraries available from from google.developers.com. On occasion
  it gets aheaad of those. It usually catches up in a few days.

  The latest generated libraries for any Google API is available automatically
  from
  https://developers.google.com/resources/api-libraries/download/<API>/<VERSION>/cpp

  For example, for Drive/v2, you would use
  https://developers.google.com/resources/api-libraries/download/drive/v2/cpp
