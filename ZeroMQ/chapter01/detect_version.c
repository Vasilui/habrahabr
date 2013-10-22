#include <stdio.h>
#include "zmq.h"

int main (int argc, char const *argv[]) {
  
  int major, minor, patch;
  zmq_version(&major, &minor, &patch);
  printf("Installed ZeroMQ version: %d.%d.%d\n", major, minor, patch);
    
  return 0;
}
