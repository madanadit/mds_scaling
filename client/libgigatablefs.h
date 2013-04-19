/* C Library for GIGA+TableFS Client
 * Wrapper over FUSE operations 
 * Author : Adit Madan (aditm)
 */
#ifndef LIBGIGATABLEFS_GIGA_H
#define LIBGIGATABLEFS_GIGA_H

#include <sys/stat.h>

int gigaInit();
void gigaDestroy();

int gigaMkdir(const char *path, mode_t mode);

#endif /*LIBGIGATABLEFS_GIGA_H*/
