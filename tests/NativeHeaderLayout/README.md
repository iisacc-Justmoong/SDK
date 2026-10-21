# Native header layout consumer

This C++23 integration test builds the source-only account, license, update and
file compatibility adapters with their colocated headers. It consumes installed
Core, FileProvider and Network packages. Each of the four public headers is also
compiled in its own translation unit. The target graph rejects Qt targets.
The smoke case checks pure account/version operations, rejects an invalid
signature and constructs an idle update manager. It does not contact services,
read credentials, write files or launch an installer.

Configure from the SDK directory with
`cmake -S tests/NativeHeaderLayout -B tests/NativeHeaderLayout/build`, supplying
dependency prefixes and `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`. Build that
`build/` directory and run its CTest. Platform libraries and the reviewed sodium
dependency remain owned by each adapter's CMake configuration.
