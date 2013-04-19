#include "libgigatablefs.h"
#include "client.h"
#include "FUSE_operations.h"
#include "common/debugging.h"
#include "common/options.h"
#include <unistd.h>
#include <sys/types.h>

/* Initialize library
 * return non-zero value on error
 */
int gigaInit()
{
  //Note: Code copied from client/client.c

  int ret = 0;
  
  // initialize logging
  char log_file[PATH_MAX] = {0};
  snprintf(log_file, sizeof(log_file),
       "%s.c.%d", DEFAULT_LOG_FILE_PATH, (int)getpid());
  if ((ret = logOpen(log_file, DEFAULT_LOG_LEVEL)) < 0) 
  { 
    fprintf(stdout, "***ERROR*** during opening log(%s) : [%s]\n", log_file, strerror(ret));
    return ret;
  }
  
  memset(&giga_options_t, 0, sizeof(struct giga_options));
  initGIGAsetting(GIGA_CLIENT, DEFAULT_MNT, CONFIG_FILE);

  GIGAinit(NULL);

  return ret;
}

void gigaDestroy()
{
  void *unused = NULL;
  GIGAdestroy(unused);
}

int gigaMkdir(const char *path, mode_t mode)
{
  return GIGAmkdir(path, mode);
}

int gigaRmdir(const char *path)
{
  return GIGArmdir(path);
}
