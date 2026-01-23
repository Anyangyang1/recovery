#include "coordinator.h"

using namespace ECProject;
int main(int argc, char **argv)
{
  if (false) {
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }
  
  std::string xml_path = "/home/anyangyang/study/recovery/clusterinfo3.xml";
  int k = stoi(argv[1]);
  int m = stoi(argv[2]);
  int w = stoi(argv[3]);
  size_t block_size = stoi(argv[4]);
  Coordinator coordinator("0.0.0.0", COORDINATOR_PORT, xml_path, k, m, w, block_size * MB, 64);
  coordinator.run();
  return 0;
}

