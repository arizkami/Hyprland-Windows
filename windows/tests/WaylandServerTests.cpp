// libwayland-server Windows backend (windows/patches/wayland): event loop and display.
#include <wayland-server.h>
#include <windows.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

static const wl_interface TEST_INTERFACE = {"hyprland_test", 4, 0, nullptr, 0, nullptr};

struct SLoop {
    wl_event_loop* loop = wl_event_loop_create();
    ~SLoop() {
        wl_event_loop_destroy(loop);
    }
};

TEST(WaylandWin32, TimersFireInDeadlineOrderAndRearm) {
    SLoop                 l;
    std::vector<int>      order;
    struct SCtx {
        std::vector<int>* order;
        int               id;
        wl_event_source*  self;
        int               rearms;
    };
    SCtx       a{&order, 1, nullptr, 1}, b{&order, 2, nullptr, 0};
    const auto fire = [](void* data) -> int {
        auto ctx = static_cast<SCtx*>(data);
        ctx->order->push_back(ctx->id);
        if (ctx->rearms-- > 0)
            wl_event_source_timer_update(ctx->self, 5);
        return 0;
    };
    a.self = wl_event_loop_add_timer(l.loop, fire, &a);
    b.self = wl_event_loop_add_timer(l.loop, fire, &b);
    ASSERT_NE(a.self, nullptr);
    wl_event_source_timer_update(a.self, 10);
    wl_event_source_timer_update(b.self, 5);

    const auto start = std::chrono::steady_clock::now();
    while (order.size() < 3 && std::chrono::steady_clock::now() - start < 2s)
        ASSERT_EQ(wl_event_loop_dispatch(l.loop, 100), 0);
    EXPECT_EQ(order, (std::vector<int>{2, 1, 1}));

    // A disarmed timer never fires.
    wl_event_source_timer_update(b.self, 5);
    wl_event_source_timer_update(b.self, 0);
    wl_event_loop_dispatch(l.loop, 30);
    EXPECT_EQ(order.size(), 3);
    wl_event_source_remove(a.self);
    wl_event_source_remove(b.self);
}

TEST(WaylandWin32, DispatchTimeoutIsBounded) {
    SLoop      l;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(wl_event_loop_dispatch(l.loop, 30), 0);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, 25ms);
    EXPECT_LT(elapsed, 1s);
}

TEST(WaylandWin32, IdleRunsOnceAndSourcesCanBeRemovedDuringDispatch) {
    SLoop l;
    int   idles = 0;
    wl_event_loop_add_idle(l.loop, [](void* data) { ++*static_cast<int*>(data); }, &idles);
    struct SCtx {
        wl_event_source* other = nullptr;
        int              fired = 0;
    } ctx;
    const auto fire = [](void* data) -> int {
        auto c = static_cast<SCtx*>(data);
        ++c->fired;
        if (c->other) {
            wl_event_source_remove(c->other);
            c->other = nullptr;
        }
        return 0;
    };
    auto first  = wl_event_loop_add_timer(l.loop, fire, &ctx);
    ctx.other   = wl_event_loop_add_timer(l.loop, fire, &ctx);
    wl_event_source_timer_update(first, 1);
    wl_event_source_timer_update(ctx.other, 1);
    Sleep(5);
    wl_event_loop_dispatch(l.loop, 0);
    EXPECT_EQ(idles, 1);
    EXPECT_EQ(ctx.fired, 1); // the second timer was removed by the first
    wl_event_loop_dispatch(l.loop, 0);
    EXPECT_EQ(idles, 1);
    wl_event_source_remove(first);
}

TEST(WaylandWin32, HandleSourceAndCrossThreadWake) {
    SLoop  l;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    int    fired = 0;
    auto   source = wl_event_loop_add_handle(
        l.loop, event,
        [](void*, void* data) -> int {
            ++*static_cast<int*>(data);
            return 0;
        },
        &fired);
    ASSERT_NE(source, nullptr);
    std::thread([event] {
        std::this_thread::sleep_for(20ms);
        SetEvent(event);
    }).detach();
    const auto start = std::chrono::steady_clock::now();
    wl_event_loop_dispatch(l.loop, 2000);
    EXPECT_EQ(fired, 1);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);

    std::thread([loop = l.loop] {
        std::this_thread::sleep_for(20ms);
        wl_event_loop_wake(loop);
    }).detach();
    const auto wakeStart = std::chrono::steady_clock::now();
    wl_event_loop_dispatch(l.loop, 2000);
    EXPECT_LT(std::chrono::steady_clock::now() - wakeStart, 1s);
    EXPECT_EQ(fired, 1);
    wl_event_source_remove(source);
    CloseHandle(event);
}

TEST(WaylandWin32, MessageSourceWakesForThreadMessages) {
    SLoop      l;
    int        pumped = 0;
    const auto pump   = [](void* data) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            ++*static_cast<int*>(data);
    };
    auto source = wl_event_loop_add_win32_messages(l.loop, pump, &pumped);
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(wl_event_loop_add_win32_messages(l.loop, pump, &pumped), nullptr); // one per loop
    PostThreadMessageW(GetCurrentThreadId(), WM_APP, 0, 0);
    const auto start = std::chrono::steady_clock::now();
    wl_event_loop_dispatch(l.loop, 2000);
    EXPECT_EQ(pumped, 1);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
    wl_event_source_remove(source);
}

TEST(WaylandWin32, SignalSource) {
    SLoop l;
    int   received = 0;
    auto  source   = wl_event_loop_add_signal(
        l.loop, SIGINT,
        [](int number, void* data) -> int {
            *static_cast<int*>(data) = number;
            return 0;
        },
        &received);
    ASSERT_NE(source, nullptr);
    std::raise(SIGINT);
    wl_event_loop_dispatch(l.loop, 1000);
    EXPECT_EQ(received, SIGINT);
    wl_event_source_remove(source);
}

TEST(WaylandWin32, DisplayGlobalsSerialsAndRunTerminate) {
    wl_display* display = wl_display_create();
    ASSERT_NE(display, nullptr);
    EXPECT_EQ(wl_display_add_socket_auto(display), nullptr);
    EXPECT_EQ(wl_client_create(display, 0), nullptr);
    EXPECT_EQ(wl_display_next_serial(display), 1u);
    EXPECT_EQ(wl_display_next_serial(display), 2u);
    EXPECT_EQ(wl_display_get_serial(display), 2u);

    auto global = wl_global_create(display, &TEST_INTERFACE, 4, nullptr, [](wl_client*, void*, uint32_t, uint32_t) {});
    ASSERT_NE(global, nullptr);
    EXPECT_EQ(wl_global_get_interface(global), &TEST_INTERFACE);
    EXPECT_EQ(wl_global_create(display, &TEST_INTERFACE, 0, nullptr, nullptr), nullptr);
    EXPECT_EQ(wl_global_create(display, &TEST_INTERFACE, TEST_INTERFACE.version + 1, nullptr, nullptr), nullptr);
    wl_global_remove(global);
    wl_global_destroy(global);

    // Terminating from a timer ends wl_display_run, as Hyprland's shutdown does.
    auto timer = wl_event_loop_add_timer(
        wl_display_get_event_loop(display),
        [](void* data) -> int {
            wl_display_terminate(static_cast<wl_display*>(data));
            return 0;
        },
        display);
    wl_event_source_timer_update(timer, 10);
    const auto start = std::chrono::steady_clock::now();
    wl_display_run(display);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);

    bool destroyed = false;
    struct SListener {
        wl_listener listener;
        bool*       flag;
    } listener{};
    listener.flag              = &destroyed;
    listener.listener.notify   = [](wl_listener* l, void*) { *reinterpret_cast<SListener*>(l)->flag = true; };
    wl_display_add_destroy_listener(display, &listener.listener);
    wl_event_source_remove(timer);
    wl_display_destroy(display);
    EXPECT_TRUE(destroyed);
}

TEST(WaylandWin32, TimerPrecisionBeatsDefaultWaitGranularity) {
    // Hyprland schedules frame work with millisecond timers; plain wait timeouts
    // round up to the ~15.6 ms system tick, the high-resolution timer does not.
    SLoop  l;
    bool   fired  = false;
    auto   source = wl_event_loop_add_timer(
        l.loop,
        [](void* data) -> int {
            *static_cast<bool*>(data) = true;
            return 0;
        },
        &fired);
    std::vector<double> samples;
    for (int i = 0; i < 9; ++i) {
        fired = false;
        wl_event_source_timer_update(source, 2);
        const auto start = std::chrono::steady_clock::now();
        while (!fired)
            wl_event_loop_dispatch(l.loop, 1000);
        samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    EXPECT_GE(samples[4], 1.5);
    EXPECT_LT(samples[4], 10.0) << "median latency of a 2 ms timer";
    wl_event_source_remove(source);
}
