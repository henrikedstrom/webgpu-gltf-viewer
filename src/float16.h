#pragma once

// Standard Library Headers
#include <cmath>
#include <cstdint>
#include <iostream>

// Float16 Class
class Float16
{
  public:
    uint16_t value;

    // Default constructor
    Float16() : value(0)
    {
    }

    // Constructor from float
    Float16(float f)
    {
        value = FloatToFloat16(f);
    }

    // Conversion to float
    operator float() const
    {
        return Float16ToFloat(value);
    }

    static uint16_t FloatToFloat16(float f);
    static float Float16ToFloat(uint16_t h);
    static void TestEdgeCases();
};
