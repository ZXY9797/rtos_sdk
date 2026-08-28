#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#if defined(__GNUC__)
#define SDK_WEAK __attribute__((weak))
#else
#define SDK_WEAK
#endif

/*
 * Bare-metal firmware has no POSIX file descriptors.  Explicit stubs keep the
 * contract deterministic and avoid silently selecting newlib's warning stubs.
 */
SDK_WEAK int _close(int file_descriptor)
{
    (void)file_descriptor;
    errno = ENOSYS;
    return -1;
}

SDK_WEAK off_t _lseek(int file_descriptor, off_t offset, int origin)
{
    (void)file_descriptor;
    (void)offset;
    (void)origin;
    errno = ENOSYS;
    return (off_t)-1;
}

SDK_WEAK ssize_t _read(int file_descriptor, void *buffer, size_t length)
{
    (void)file_descriptor;
    (void)buffer;
    (void)length;
    errno = ENOSYS;
    return (ssize_t)-1;
}

SDK_WEAK ssize_t _write(int file_descriptor, const void *buffer,
                        size_t length)
{
    (void)file_descriptor;
    (void)buffer;
    (void)length;
    errno = ENOSYS;
    return (ssize_t)-1;
}
