# tailzero
Windows command-line app to look for files ending in zeros (thus perhaps corrupt by a failed copy).

Enumerate all files under a given path and checks if up to the last 8k bytes are 0-filled.

Files can end up this way if a copy is interrupted or if networking hardware is in a bad state.

Usage information:

    usage: tailzero <path>
      looks for files with zero tails indicating potential corruption

