#pragma once

#include <cstdint>

class Application
{
  public:
    Application(uint32_t width, uint32_t height);
    void Run();

  private:
    uint32_t m_width;
    uint32_t m_height;
};