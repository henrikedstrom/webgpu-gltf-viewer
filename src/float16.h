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

    static uint16_t FloatToFloat16(float f)
    {
        uint32_t bits = *(reinterpret_cast<uint32_t *>(&f)); // Get float bits
        uint32_t sign = (bits >> 16) & 0x8000;               // Extract sign
        int32_t exponent = ((bits >> 23) & 0xFF) - 127 + 15; // Adjust exponent (127 -> 15 bias)
        uint32_t mantissa = bits & 0x007FFFFF;               // Extract mantissa

        const float maxFloat16 = 65504.0f; // Maximum representable float16 value
        if (f > maxFloat16)
        {
            f = maxFloat16; // Clamp positive overflow
            bits = *(reinterpret_cast<uint32_t *>(&f));
            exponent = ((bits >> 23) & 0xFF) - 127 + 15;
            mantissa = bits & 0x007FFFFF;
        }
        else if (f < -maxFloat16)
        {
            f = -maxFloat16; // Clamp negative overflow
            bits = *(reinterpret_cast<uint32_t *>(&f));
            exponent = ((bits >> 23) & 0xFF) - 127 + 15;
            mantissa = bits & 0x007FFFFF;
        }

        if (exponent == 0xFF - 127 + 15)
        { // Check if the input is NaN or Inf
            if (mantissa != 0)
            {
                // NaN: Preserve the payload as much as possible
                return sign | 0x7C00 | (mantissa >> 13) | 0x0200; // Ensure at least one bit in the mantissa
            }
            else
            {
                // Inf
                return sign | 0x7C00;
            }
        }

        if (exponent <= 0)
        {
            // Subnormal or zero
            if (exponent < -10)
            {
                return sign; // Too small to represent, becomes zero
            }
            // Subnormal value
            mantissa = (mantissa | 0x00800000) >> (1 - exponent);
            return sign | (mantissa >> 13);
        }
        else if (exponent >= 31)
        {
            // Overflow, becomes Inf
            return sign | 0x7C00;
        }

        // Normalized value
        uint16_t result = sign | (exponent << 10) | (mantissa >> 13);

        // Rounding: Add 1 to the least significant bit if the next bit is 1
        if (mantissa & 0x00001000)
        { // Check bit 12 (rounding bit)
            result += 1;
        }

        return result;
    }

    // Static conversion: float16 -> float
    static float Float16ToFloat(uint16_t h)
    {
        uint32_t sign = (h & 0x8000) << 16;     // Extract sign and shift to 32-bit position
        uint32_t exponent = (h & 0x7C00) >> 10; // Extract exponent
        uint32_t mantissa = h & 0x03FF;         // Extract mantissa

        if (exponent == 0)
        {
            // Subnormal or zero
            if (mantissa == 0)
                return *(reinterpret_cast<float *>(&sign)); // Zero
            // Normalize subnormal
            while ((mantissa & 0x0400) == 0)
            {
                mantissa <<= 1;
                exponent -= 1;
            }
            exponent += 1;
            mantissa &= ~0x0400;
        }
        else if (exponent == 0x1F)
        {
            // Inf or NaN
            uint32_t inf_nan = sign | 0x7F800000 | (mantissa << 13);
            return *(reinterpret_cast<float *>(&inf_nan));
        }

        // Normalized value
        exponent = exponent + 112; // Adjust exponent (15 -> 127)
        uint32_t result = sign | (exponent << 23) | (mantissa << 13);
        return *(reinterpret_cast<float *>(&result));
    }

    static void TestEdgeCases();
};
