#include "pch.h"
#include <Engine/Event.h>

#include <string>
#include <functional>
#include <iostream>
#include <sstream>

namespace VoxelEngine
{
    std::string Event::ToString() const
    {
        return GetName();
    }

    std::ostream &operator<<(std::ostream &os, const Event &e)
    {
        return os << e.ToString();
    }
}
