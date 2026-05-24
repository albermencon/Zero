#include "pch.h"
#include <Engine/Event.h>
#include <string>
#include <iostream>

namespace Zero
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
