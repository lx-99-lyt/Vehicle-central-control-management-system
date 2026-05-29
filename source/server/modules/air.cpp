// Air Module —— 空调控制子进程
#include "AirModule.hpp"

int main() { setupModuleSignalHandlers(); AirModule().start(); return 0; }
