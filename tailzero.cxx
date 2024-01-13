// It's by no means a sure thing that a file is corrupted if it ends with zeros.
// WAV files are just like this by design along with other file formats.
// But it's often true that when copying files, errors result in partial copies and zeroes at the end of files.

#define _CRT_SECURE_NO_WARNINGS
#define PARALLEL_EXECUTION

#include <windows.h>

#include <vector>
#include <chrono>
#include <stdlib.h>
#include <stdio.h>
#include <ppl.h>
#include <process.h>

using namespace std;
using namespace std::chrono;
using namespace concurrency;

#include <djl_os.hxx>
#include <djlenum.hxx>

std::mutex g_mtx;
CDJLTrace tracer;

void usage()
{
    printf( "usage: tailzero <path>\n" );
    printf( "  looks for files with zero tails indicating potential corruption\n" );
    exit( 1 );
} //usage

int wmain( int argc, WCHAR * argv[] )
{
    WCHAR * path = (WCHAR *) L".";
    if ( 2 == argc )
        path = argv[1];
    else if ( argc > 2 )
        usage();

    CPathArray paths;
    CEnumFolder enumerate( true, &paths, 0, 0 );
    enumerate.Enumerate( path, 0 );

    if ( 0 == paths.Count() )
    {
        printf( "no files found\n" );
        usage();
    }

    const LONGLONG tailLen = 8192;
    byte buf[ tailLen ];
    size_t found = 0;
    size_t cPaths = paths.Count();
    printf( "looking at %zu files\n", cPaths );

#ifdef PARALLEL_EXECUTION
    parallel_for( (size_t) 0, cPaths, [&] (size_t i)
#else
    for ( size_t i = 0; i < cPaths; i++ )
#endif
    {
        WCHAR const * pwc = paths.Get( i );

        HANDLE h = CreateFile( pwc, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0 );
        if ( INVALID_HANDLE_VALUE != h )
        {
            LARGE_INTEGER fileSize;
            BOOL ok = GetFileSizeEx( h, & fileSize );
            if ( ok )
            {
                if ( 0 != fileSize.QuadPart )
                {
                    LONG toCheck = (LONG) get_min( fileSize.QuadPart, tailLen );
                    LARGE_INTEGER seek;
                    seek.QuadPart = -toCheck;
                    ok = SetFilePointerEx( h, seek, 0, FILE_END );
                    if ( ok )
                    {
                        ok = ReadFile( h, buf, toCheck, 0, 0 );
                        if ( ok )
                        {
                            bool allZero = true;
                            for ( long b = 0; b < toCheck; b++ )
                            {
                                if ( 0 != buf[ b ] )
                                {
                                    allZero = false;
                                    break;
                                }
                            }
        
                            if ( allZero )
                            {
                                lock_guard<mutex> lock( g_mtx );
                                found++;
                                printf( "the last %lu bytes are zero: %ws\n", toCheck, pwc );
                            }
                        }
                        else
                        {
                            lock_guard<mutex> lock( g_mtx );
                            printf( "can't read from file %ws, error %d\n", pwc, GetLastError() );
                        }
                    }
                    else
                    {
                        lock_guard<mutex> lock( g_mtx );
                        printf( "can't seek to %ld bytes from end of file, error %d\n", toCheck, GetLastError() );
                    }
                }
            }
            else
            {
                lock_guard<mutex> lock( g_mtx );
                printf( "can't get file length for file %ws, error %d\n", pwc, GetLastError() );
            }

            CloseHandle( h );
        }
    }
#ifdef PARALLEL_EXECUTION
    );
#endif

    printf( "found %zu files with a zero tail out of %llu\n", found, cPaths );
} //main
