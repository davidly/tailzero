// It's by no means a sure thing that a file is corrupted if it ends with zeros.
// WAV files are just like this by design along with other file formats.
// But it's often true that when copying files, errors result in partial copies and zeroes at the end of files.

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>

#include <vector>
#include <chrono>
#include <stdlib.h>
#include <stdio.h>
#include <process.h>
#include <ppl.h>

using namespace std;
using namespace std::chrono;
using namespace concurrency;

#include <djl_os.hxx>
#include <djlenum.hxx>

CDJLTrace tracer;
static std::mutex g_mtx;
static bool muteErrors = false;
const LONGLONG tailLen = 8192;

void usage()
{
    printf( "usage: tailzero [-m] [-s] <path>\n" );
    printf( "  looks for files with zero tails indicating potential corruption.\n" );
    printf( "  arguments:        -m    mute errors including access denied.\n" );
    printf( "                    -s    single-threaded, not multi-threaded search.\n" );
    printf( "                    path  the path to search. default is current directory.\n" );
    printf( "  e.g.:   tailzero\n" );
    printf( "          tailzero c:\\foo\n" );
    printf( "          tailzero -s c:\\foo\n" );
    printf( "          tailzero \\\\server\\share\\folder\n" );
    exit( 1 );
} //usage

class XHandle
{
    private:
        HANDLE _h;
    public:
        XHandle( HANDLE h = INVALID_HANDLE_VALUE ) : _h( h ) {};
        ~XHandle() { if ( ( INVALID_HANDLE_VALUE != _h ) && ( 0 != _h ) ) CloseHandle( _h ); }
};

const WCHAR * WinErrorString( DWORD dwerr = GetLastError() )
{
    static WCHAR awc[ MAX_PATH ];
    awc[ 0 ] = 0; // in case FormatMessage fails
    FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM, 0, dwerr, 0, awc, _countof( awc ), 0 );
    size_t len = wcslen( awc );
    for ( size_t i = 0; i < len; i++ )
        if ( L'\r' == awc[ i ] || L'\n' == awc[ i ] )
            awc[ i ] = 0;
    return awc;
} //WinErrorString

void search_folder( const WCHAR * pwc, size_t & found )
{
    byte buf[ tailLen ];
    HANDLE h = CreateFile( pwc, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0 );
    if ( INVALID_HANDLE_VALUE != h )
    {
        XHandle xh( h );
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
                            printf( "the last %4lu bytes are zero: %ws\n", toCheck, pwc );
                        }
                    }
                    else if ( !muteErrors )
                    {
                        DWORD dwerr = GetLastError();
                        lock_guard<mutex> lock( g_mtx );
                        printf( "error %d %ws -- can't read %ld bytes from file %ws\n", dwerr, WinErrorString( dwerr ), toCheck, pwc );
                    }
                }
                else if ( !muteErrors )
                {
                    DWORD dwerr = GetLastError();
                    lock_guard<mutex> lock( g_mtx );
                    printf( "can't seek to %ld bytes from end of file, error %d %ws\n", toCheck, dwerr, WinErrorString( dwerr ) );
                }
            }
        }
        else if ( !muteErrors )
        {
            DWORD dwerr = GetLastError();
            lock_guard<mutex> lock( g_mtx );
            printf( "can't get file length for file %ws, error %d %ws\n", pwc, dwerr, WinErrorString( dwerr ) );
        }
    }
    else if ( !muteErrors )
    {
        DWORD dwerr = GetLastError();
        lock_guard<mutex> lock( g_mtx );
        printf( "can't open file %ws, error %d %ws\n", pwc, dwerr, WinErrorString( dwerr ) );
    }
} //search_folder

int wmain( int argc, WCHAR * argv[] )
{
    WCHAR * path = (WCHAR *) L".";
    bool parallel = true;

    for ( int i = 1; i < argc; i++ )
    {
        if ( '-' == argv[i][0] || '/' == argv[i][0] )
        {
            WCHAR a = argv[i][1];

            if ( 'm' == a )
                muteErrors = true;
            else if ( 's' == a )
                parallel = false;
            else
            {
                printf( "invalid argument\n" );
                usage();
            }
        }
        else
            path = argv[i];
    }

    WCHAR fullPath[ MAX_PATH ];
    DWORD result = GetFullPathName( path, _countof( fullPath ), fullPath, 0 );
    if ( 0 == result )
    {
        DWORD dwerr = GetLastError();
        printf( "error %d %ws -- unable to get full path for %ws\n", dwerr, WinErrorString( dwerr ), path );
        usage();
    }

    result = GetFileAttributes( fullPath );
    if ( INVALID_FILE_ATTRIBUTES == result )
    {
        DWORD dwerr = GetLastError();
        printf( "error %d %ws -- can't find path %ws\n", dwerr, WinErrorString( dwerr ), fullPath );
        usage();
    }

    if ( ! ( result & FILE_ATTRIBUTE_DIRECTORY ) )
    {
        printf( "error -- path isn't a directory: %ws\n", fullPath );
        usage();
    }

    CPathArray paths;
    CEnumFolder enumerate( true, &paths, 0, 0 );
    enumerate.Enumerate( fullPath, 0 );

    if ( 0 == paths.Count() )
    {
        printf( "no files found\n" );
        usage();
    }

    size_t found = 0;
    size_t cPaths = paths.Count();
    printf( "looking at %zu files in folder %ws\n", cPaths, fullPath );

    if ( parallel )
    {
        parallel_for( (size_t) 0, cPaths, [&] (size_t i)
        {
            search_folder( paths.Get( i ), found );
        } );
    }
    else
    {
        for ( size_t i = 0; i < cPaths; i++ )
            search_folder( paths.Get( i ), found );
    }

    printf( "found %zu files with a zero tail out of %zu\n", found, cPaths );
} //wmain
