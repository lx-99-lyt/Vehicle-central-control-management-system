// Status Module —— 车辆状态监控子进程
#include "StatusModule.hpp"

int main() { setupModuleSignalHandlers(); StatusModule().start(); return 0; }
