#include "Panel.hpp"
#include <iostream>
#include <stdexcept>

using namespace WindowsUI;
static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        CPanel panel;
        panel.layout(520, 610, {});
        require(panel.hit(100, 120) == EAction::Toggle, "Enable control hit");
        require(panel.hit(18, 102) == EAction::None, "Transparent rounded corner must not hit");
        require(panel.hit(-1, 120) == EAction::None, "Outside clip must not hit");
        require(panel.hit(100, 80) == EAction::None, "Header must not be interactive");
        const auto& commands = panel.commands();
        require(commands.back().box.y + commands.back().box.height < 610, "Initial DIP height must contain all panel text");
        panel.layout(200, 115, {});
        require(panel.hit(100, 120) == EAction::None, "Invisible portion of clipped button must not hit");
        require(panel.hit(100, 110) == EAction::Toggle, "Visible portion of clipped button must hit");
        panel.nextFocus();
        require(panel.focusedAction() == EAction::Layout, "Keyboard navigation");
        std::cout << "Yoga layout, rounded hit regions and clip containment passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
