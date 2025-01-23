// Standard Library Headers
#include <iomanip>
#include <iostream>

// Project Headers
#include "float16.h"

void Float16::TestEdgeCases()
{
    // Test the Float16 conversion functions
    std::cout << "Float16 Conversion Tests:\n";
    std::cout << "-------------------------\n";

    float testValues[] = {0.0f, 1.0f, 65504.0f, 70000.0f, -70000.0f, -65504.0f, 1e-8f, INFINITY, -INFINITY, NAN};

    for (float value : testValues)
    {
        uint16_t half = FloatToFloat16(value);
        float backToFloat = Float16::Float16ToFloat(half);
        std::cout << "Original: " << std::setw(10) << value << " -> Half: 0x" << std::hex << half
                  << " -> Restored: " << std::dec << backToFloat << "\n";
    }
}