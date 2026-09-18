Place libicsneo C API shared library here (not bundled with openbus):

  Windows: icsneoc.dll
  Linux:   libicsneoc.so

Build from https://github.com/intrepidcs/libicsneo (LIBICSNEO_BUILD_ICSNEOC=ON)
or install a release that provides the icsneoc export set.

Load order: this folder -> application directory -> system PATH / ld cache.

Note: Vehicle Spy's legacy icsneo40.dll is a different ABI and is NOT used.
