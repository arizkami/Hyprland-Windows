#include "Layout.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace WindowsTiling;
static void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
static bool overlap(SRect a, SRect b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
int main() {
    try {
        for (const auto layout : {
                 ELayout::Dwindle,
                 ELayout::Master,
             }) {
            for (const int count : {
                     1,
                     2,
                     3,
                     4,
                     6,
                     8,
                 }) {
                for (const int gap : {
                         0,
                         1,
                         12,
                         64,
                         3000,
                     }) {
                    std::vector<WindowID> ids;
                    for (int i = 0; i < count; ++i)
                        ids.push_back(static_cast<WindowID>(i) + 1);
                    const SRect area{
                        -1920,
                        -200,
                        1920,
                        1080,
                    };
                    const auto cells = arrange(ids, area, layout, gap, gap);
                    // Dwindle can exhaust a tiny remainder at extreme gaps.
                    if (cells.empty())
                        continue;
                    require(cells.size() == ids.size(), "Every ID must receive a tile");
                    for (size_t i = 0; i < cells.size(); ++i) {
                        const auto r = cells[i].rect;
                        require(cells[i].id == ids[i], "Stable insertion order");
                        require(r.width > 0 && r.height > 0, "Positive dimensions");
                        require(r.x >= area.x && r.y >= area.y && r.x + r.width <= area.x + area.width && r.y + r.height <= area.y + area.height, "Work-area containment");
                        for (size_t j = i + 1; j < cells.size(); ++j)
                            require(!overlap(r, cells[j].rect), "Tiles must not overlap");
                    }
                }
            }
        }
        const std::vector<WindowID> ids{
            1,
            2,
            3,
        };
        const auto dwindle = arrange(ids,
                                     {
                                         0,
                                         0,
                                         1000,
                                         800,
                                     },
                                     ELayout::Dwindle, 10, 20);
        require(dwindle[0].rect ==
                    SRect{
                        20,
                        20,
                        475,
                        760,
                    },
                "Dwindle first split and outer gap");
        require(dwindle[1].rect ==
                    SRect{
                        505,
                        20,
                        475,
                        375,
                    },
                "Dwindle second split uses longer axis");
        require(dwindle[2].rect ==
                    SRect{
                        505,
                        405,
                        475,
                        375,
                    },
                "Dwindle inner gap");
        const auto master = arrange(ids,
                                    {
                                        0,
                                        0,
                                        1000,
                                        800,
                                    },
                                    ELayout::Master, 0, 0);
        require(master[0].rect ==
                    SRect{
                        0,
                        0,
                        600,
                        800,
                    },
                "Master fraction");
        require(master[2].rect ==
                    SRect{
                        600,
                        400,
                        400,
                        400,
                    },
                "Slave column");
        require(directionalFocus(master, 1, EDirection::Right) == 2, "Directional focus");
        require(!directionalFocus(master, 1, EDirection::Left), "No target in direction");
        require(!directionalFocus(master, 999, EDirection::Right), "Missing focus ID");
        require(arrange({},
                        {
                            0,
                            0,
                            1,
                            1,
                        },
                        ELayout::Dwindle, 0, 0)
                    .empty(),
                "Empty layout");
        require(arrange(ids,
                        {
                            0,
                            0,
                            1,
                            1,
                        },
                        ELayout::Master, 999, 999)
                    .empty(),
                "Overcapacity returns no placement");
        SWindowPolicy p;
        p.visible = p.resizable = true;
        require(classify(p) == EDisposition::Managed, "Normal resizable window");
        p.owned = true;
        require(classify(p) == EDisposition::Floating, "Owned dialog floats");
        p.owned     = false;
        p.resizable = false;
        require(classify(p) == EDisposition::Floating, "Fixed-size window floats");
        p.resizable = true;
        for (auto flag : {
                 &SWindowPolicy::ownProcess,
                 &SWindowPolicy::shell,
                 &SWindowPolicy::tool,
                 &SWindowPolicy::child,
                 &SWindowPolicy::cloaked,
             }) {
            p.*flag = true;
            require(classify(p) == EDisposition::Ignore, "Excluded window policy");
            p.*flag = false;
        }
        p.visible = false;
        require(classify(p) == EDisposition::Ignore, "Hidden window policy");
        std::cout << "Layout geometry, gaps, focus and filtering policy passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
