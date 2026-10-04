// aquamarine Win32 backend (windows/patches/aquamarine): monitors as outputs, frame pacing.
#include <aquamarine/backend/Backend.hpp>
#include <aquamarine/backend/Win32.hpp>
#include <aquamarine/output/Output.hpp>
#include <windows.h>
#include <gtest/gtest.h>

#include <chrono>
#include <vector>

using namespace Aquamarine;
using namespace Hyprutils::Memory;
using namespace std::chrono_literals;
template <typename T>
using SP = CSharedPointer<T>;

// Waits on the backend's poll handles and dispatches them, as the compositor's loop does.
static void pump(const SP<CBackend>& aqBackend, std::chrono::milliseconds duration) {
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        auto                 fds = aqBackend->getPollFDs();
        std::vector<HANDLE>  handles;
        std::vector<SP<SPollFD>> owners;
        for (const auto& fd : fds) {
            if (!fd->handle)
                continue;
            handles.push_back(fd->handle);
            owners.push_back(fd);
        }
        const auto left    = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count();
        const auto waited  = MsgWaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, static_cast<DWORD>(std::max<long long>(0, left)), QS_ALLINPUT);
        if (waited < WAIT_OBJECT_0 + handles.size())
            owners[waited - WAIT_OBJECT_0]->onSignal();
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&message);
    }
}

static SP<CBackend> createBackend(std::vector<SP<IOutput>>& outputs) {
    SBackendImplementationOptions win32;
    win32.backendType        = AQ_BACKEND_WIN32;
    win32.backendRequestMode = AQ_BACKEND_REQUEST_MANDATORY;
    SBackendOptions options;
    auto            backend = CBackend::create({win32}, options);
    if (backend)
        backend->events.newOutput.listenStatic([&outputs](SP<IOutput> output) { outputs.push_back(output); });
    return backend;
}

TEST(AquamarineWin32, OutputsMirrorAttachedMonitors) {
    std::vector<SP<IOutput>> outputs;
    auto                     backend = createBackend(outputs);
    ASSERT_TRUE(backend);
    ASSERT_TRUE(backend->start());
    EXPECT_EQ(backend->getImplementations().size(), 1u);
    EXPECT_EQ(backend->getImplementations().at(0)->type(), AQ_BACKEND_WIN32);
    EXPECT_FALSE(backend->hasSession());
    EXPECT_EQ(backend->drmFD(), -1);

    ASSERT_EQ(outputs.size(), static_cast<size_t>(GetSystemMetrics(SM_CMONITORS)));
    for (const auto& output : outputs) {
        EXPECT_TRUE(output->name.starts_with("DISPLAY")) << output->name;
        EXPECT_TRUE(output->enabled);
        ASSERT_FALSE(output->modes.empty());
        const auto preferred = output->preferredMode();
        ASSERT_TRUE(preferred);
        EXPECT_GT(preferred->pixelSize.x, 0);
        EXPECT_GE(preferred->refreshRate, 23000u);

        // Committing records state without touching the real display mode.
        output->state->setEnabled(true);
        output->state->setMode(preferred);
        EXPECT_TRUE(output->test());
        EXPECT_TRUE(output->commit());
        EXPECT_EQ(output->state->state().mode, preferred);
    }

    // Monitor refresh leaves existing outputs in place.
    auto win32 = reinterpret_cast<CWin32Backend*>(backend->getImplementations().at(0).get());
    win32->refreshOutputs();
    EXPECT_EQ(win32->getOutputs().size(), outputs.size());
}

TEST(AquamarineWin32, FramesArePacedAtTheRefreshRate) {
    std::vector<SP<IOutput>> outputs;
    auto                     backend = createBackend(outputs);
    ASSERT_TRUE(backend && backend->start());
    ASSERT_FALSE(outputs.empty());

    auto output = outputs.front();
    int  frames = 0;
    auto listener = output->events.frame.listen([&frames] { ++frames; });
    output->state->setMode(output->preferredMode());
    output->commit();

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < 500ms) {
        if (!output->pendingIdleFrame())
            output->scheduleFrame(IOutput::AQ_SCHEDULE_ANIMATION);
        pump(backend, 5ms);
    }

    // 500 ms at the preferred refresh rate, with generous slack for a loaded machine.
    const double expected = output->preferredMode()->refreshRate / 1000.0 / 2.0;
    EXPECT_GT(frames, expected * 0.5) << "frames in 500 ms";
    EXPECT_LT(frames, expected * 1.3) << "frames in 500 ms";
}

TEST(AquamarineWin32, UnsupportedLinuxBackendsAreRejected) {
    SBackendImplementationOptions drm;
    drm.backendType        = AQ_BACKEND_DRM;
    drm.backendRequestMode = AQ_BACKEND_REQUEST_MANDATORY;
    auto backend           = CBackend::create({drm}, SBackendOptions{});
    ASSERT_TRUE(backend);
    EXPECT_TRUE(backend->getImplementations().empty());
    EXPECT_FALSE(backend->start());
}
