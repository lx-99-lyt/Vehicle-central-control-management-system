// Door Module —— 车门控制子进程
#include "DoorModule.hpp"

int main() { setupModuleSignalHandlers(); DoorModule().start(); return 0; }
