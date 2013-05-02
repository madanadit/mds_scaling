#include "hdfs.h"

int main(int argc, char **argv) {

  hdfsFS fs =hdfsConnect("10.1.1.108", 8020);
  if (!fs) {
    fprintf(stderr, "Cannot connect to HDFS.\n");

    fprintf(stdout, "Connection failed!\n");
    exit(-1);
  }


  fprintf(stdout, "Connection successful!\n");
  
  char* fileName = "/demo_c.txt";
  char* message = "Welcome to HDFS C API!!!";
  int size = strlen(message);

  int exists = hdfsExists(fs, fileName);

  if (exists > -1) {
    fprintf(stdout, "File %s exists!\n", fileName);
  } else{
    fprintf(stdout, "Opening File %s!\n", fileName);
    // Create and open file for writing
    hdfsFile outFile = hdfsOpenFile(fs, fileName, O_WRONLY|O_CREAT, 0, 0, 0);
    if (!outFile) {
      fprintf(stdout, "Failed to open file %s!\n", fileName);
      fprintf(stderr, "Failed to open %s for writing!\n", fileName);
            exit(-2);
    }

    fprintf(stdout, "Writing File %s!\n", fileName);
    // write to file
    hdfsWrite(fs, outFile, (void*)message, size);
    fprintf(stdout, "Closing File %s!\n", fileName);
    hdfsCloseFile(fs, outFile);
  }
}
