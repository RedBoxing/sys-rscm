#include "debug.hpp"

u64 attachedProcessId = 0;
Handle debugHandle;
Status status = Status::Stopped;

std::vector<Client *> clients;