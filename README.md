# tailzero
Windows command-line app to look for files ending in zeros (thus perhaps corrupted by a failed copy).

Enumerate all files under a given path and checks if up to the last 8k bytes are 0-filled.

Files can end up this way if a copy is interrupted or if networking hardware is in a bad state.

Usage information:

    usage: tailzero [-s] <path>
      looks for files with zero tails indicating potential corruption.
      arguments:        -m    mute errors including access denied.
                        -s    single-threaded, not multi-threaded search.
                        -t:X  tail length 1..16384. default is 8192.
                        path  the path to search. default is current directory.
      e.g.:   tailzero
              tailzero c:\foo
              tailzero -s c:\foo
              tailzero \\server\share\folder

