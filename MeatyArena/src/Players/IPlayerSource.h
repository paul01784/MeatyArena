#pragma once

#include "PlayerModel.h"

#include <string>
#include <vector>

class IPlayerSource
{
public:
    virtual ~IPlayerSource() = default;
    virtual bool ReadPlayers(std::vector<PlayerSnapshot>& players, std::string& error) = 0;
};
