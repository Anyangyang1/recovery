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
  
  std::string xml_path = "/home/anyangyang/study/recovery/clusterinfo.xml";
  Coordinator coordinator("0.0.0.0", COORDINATOR_PORT, xml_path);
  coordinator.run();
  return 0;
}

