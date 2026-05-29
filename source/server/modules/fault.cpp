// Fault Module —— 故障诊断子进程
#include "FaultModule.hpp"

int main() { setupModuleSignalHandlers(); FaultModule().start(); return 0; }
