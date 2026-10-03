#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

namespace WindowsUI {
    struct SBox {
        float x = 0, y = 0, width = 0, height = 0;
    };
    struct SDrawCommand {
        SBox         box;
        SBox         clip;
        std::wstring text;
        bool         active = false;
        float        radius = 8;
    };
    class CGraphicEngineCore {
      public:
        explicit CGraphicEngineCore(HWND window);
        ~CGraphicEngineCore();
        void               resize(unsigned width, unsigned height, unsigned dpi);
        bool               draw(const std::vector<SDrawCommand>& commands);
        unsigned long long frames() const;

      private:
        struct SResources;
        std::unique_ptr<SResources> m_resources;
        HWND                        m_window = nullptr;
        unsigned                    m_width = 1, m_height = 1, m_dpi = 96;
        unsigned long long          m_frames = 0;
        void                        initialize();
        void                        createTarget();
    };
}
