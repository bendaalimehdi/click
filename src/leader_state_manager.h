#pragma once

#include <Arduino.h>
#include <vector>
#include "protocol_types.h"

class LeaderStateManager {
public:
    bool begin();
    bool saveNodes(const std::vector<NodeRecord>& nodes);
    bool loadNodes(std::vector<NodeRecord>& nodes);
    String getLastError() const;

private:
    String _lastError;
    const char* _stateFile = "/leader_state.json";
};