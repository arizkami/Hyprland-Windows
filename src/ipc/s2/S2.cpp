#include "S2.hpp"
#include <algorithm>

#ifdef _WIN32
#include "../../../windows/platform/win32/ipc/Sockets.hpp"
#else
#include "Unix.hpp"
#endif

using namespace IPC;
using namespace IPC::Socket2;

UP<CSocket2>& IPC::Socket2::sock() {
    static auto p = makeUnique<CSocket2>();
    return p;
}

#ifdef _WIN32
CSocket2::CSocket2() : m_impl(makeWindowsImplementation())
#else
CSocket2::CSocket2() : m_impl(makeUnique<CUnixImpl>())
#endif
 {
    ;
}

static std::string formatEvent(SEvent&& event) {
    std::string_view data        = event.data;
    const auto       LEN         = event.event.length();
    auto             eventString = std::format("{}>>{}\n", std::move(event.event), data.substr(0, 1024));
    std::replace(eventString.begin() + LEN + 2, eventString.end() - 1, '\n', ' ');
    return eventString;
}

void CSocket2::postEvent(SEvent&& event) {
    auto str = formatEvent(std::move(event));
    m_impl->send(std::move(str));
}
