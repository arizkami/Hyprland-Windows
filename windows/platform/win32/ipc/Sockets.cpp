#include "Sockets.hpp"
#include "Pipe.hpp"

#include "../../../../src/Compositor.hpp"
#include "../../../../src/debug/log/RollingLogFollow.hpp"
#include "../../../../src/ipc/s1/Impl.hpp"
#include "../../../../src/ipc/s2/Impl.hpp"
#include "../../../../src/managers/eventLoop/EventLoopManager.hpp"
#include "../../../../src/managers/eventLoop/EventLoopTimer.hpp"

#include <algorithm>
#include <chrono>
#include <ranges>
#include <vector>

using namespace WindowsPlatform;

// Socket1: requests ------------------------------------------------------------------

namespace IPC::Socket1 {
    static constexpr size_t REQUEST_LIMIT      = 1024 * 1024;
    static constexpr size_t FOLLOW_QUEUE_LIMIT = 64 * 1024;

    class CPipeImpl;

    class CPipePeer {
      public:
        enum class eState : uint8_t {
            READING = 0,
            DEFERRED,
            WRITING,
            FOLLOWING,
        };

        UP<CPipeConnection> connection;
        SP<CEventLoopTimer> requestTimer;
        eState              state     = eState::READING;
        eReplyMode          replyMode = REPLY_MODE_CLOSE;
        bool                removed   = false;

        ~CPipePeer() {
            if (state == eState::FOLLOWING && connection)
                Log::SRollingLogFollow::get().stopFor(followId());
            if (requestTimer)
                requestTimer->cancel();
        }

        int followId() const {
            return sc<int>(connection->id());
        }
    };

    class CPipeImpl : public IImplementation {
      public:
        CPipeImpl() = default;

        ~CPipeImpl() override {
            *m_alive = false;
            m_followTimer.reset();
            m_peers.clear();
            m_listener.reset();
        }

        void start(FRequestHandler&& handler) override {
            m_requestHandler = std::move(handler);
            const auto NAME  = hyprlandPipeName(g_pCompositor->m_instanceSignature, ".socket.sock");
            m_listener       = makeUnique<CPipeListener>(NAME, g_pCompositor->m_wlEventLoop, [this](void* pipe) { onConnection(pipe); });
            if (!m_listener->start()) {
                LOG(Log::ERR, "[Socket1::Pipe] couldn't start the request pipe");
                m_listener.reset();
                return;
            }
            LOG(Log::DEBUG, "[Socket1::Pipe] listening on {}", g_pCompositor->m_instanceSignature);
        }

      private:
        void onConnection(void* pipe) {
            auto  peer = makeShared<CPipePeer>();
            WP<CPipePeer> weak = peer;
            peer->connection   = makeUnique<CPipeConnection>(pipe, g_pCompositor->m_wlEventLoop,
                                                           CPipeConnection::SCallbacks{
                                                               .readable = [this, weak] { onReadable(weak.lock()); },
                                                               .drained  = [this, weak] { handleResponse(weak.lock()); },
                                                               .closed   = [this, weak] { removeLater(weak.lock()); },
                                                           });
            if (!peer->connection->good())
                return;

            // Like the Unix socket: a client that sends nothing is dropped after 5s.
            peer->requestTimer = makeShared<CEventLoopTimer>(
                std::chrono::seconds(5),
                [this, weak](SP<CEventLoopTimer>, void*) {
                    if (const auto PEER = weak.lock(); PEER && PEER->state == CPipePeer::eState::READING)
                        removeLater(PEER);
                },
                nullptr);
            g_pEventLoopManager->addTimer(peer->requestTimer);
            m_peers.emplace_back(std::move(peer));
        }

        void onReadable(const SP<CPipePeer>& peer) {
            if (!peer || peer->removed)
                return;
            if (peer->state != CPipePeer::eState::READING) {
                peer->connection->takeInput(); // nothing else is expected after a request
                return;
            }
            if (peer->connection->inputSize() > REQUEST_LIMIT) {
                removeLater(peer);
                return;
            }

            auto request = peer->connection->takeInput();
            if (request.empty())
                return;
            peer->state = CPipePeer::eState::DEFERRED;
            if (peer->requestTimer)
                peer->requestTimer->cancel();

            setResponse(peer, m_requestHandler(std::move(request), sc<pid_t>(peer->connection->clientPid())));
        }

        void setResponse(const SP<CPipePeer>& peer, SResponse&& response) {
            peer->replyMode = response.mode;

            if (std::holds_alternative<SP<CPromise<std::string>>>(response.result)) {
                peer->state        = CPipePeer::eState::DEFERRED;
                const auto PROMISE = std::get<SP<CPromise<std::string>>>(std::move(response.result));
                PROMISE->then([this, weak = WP<CPipePeer>{peer}, alive = WP<bool>{m_alive}, mode = response.mode](SP<CPromiseResult<std::string>> result) {
                    const auto PEER = weak.lock();
                    if (!alive || !*alive.lock() || !PEER || PEER->removed)
                        return;
                    setResponse(PEER, SResponse{result->hasError() ? result->error() : result->result(), mode});
                });
                return;
            }

            peer->state = CPipePeer::eState::WRITING;
            auto output = std::get<std::string>(std::move(response.result));
            if (!output.empty() && !peer->connection->write(std::move(output))) {
                removeLater(peer);
                return;
            }
            if (peer->connection->queued() == 0)
                handleResponse(peer);
        }

        void handleResponse(const SP<CPipePeer>& peer) {
            if (!peer || peer->removed || peer->state != CPipePeer::eState::WRITING || peer->connection->queued())
                return;
            if (peer->replyMode == REPLY_MODE_CLOSE)
                removeLater(peer);
            else
                beginFollowing(peer);
        }

        void beginFollowing(const SP<CPipePeer>& peer) {
            peer->state = CPipePeer::eState::FOLLOWING;
            Log::SRollingLogFollow::get().startFor(peer->followId());

            if (!m_followTimer) {
                m_followTimer = makeShared<CEventLoopTimer>(
                    std::chrono::milliseconds(100),
                    [this](SP<CEventLoopTimer> self, void*) {
                        flushFollowers();
                        self->updateTimeout(hasFollowers() ? std::optional<Time::steady_dur>{std::chrono::milliseconds(100)} : std::nullopt);
                    },
                    nullptr);
                g_pEventLoopManager->addTimer(m_followTimer);
            } else
                m_followTimer->updateTimeout(std::chrono::milliseconds(100));
        }

        void flushFollowers() {
            for (const auto& peer : m_peers) {
                if (peer->removed || peer->state != CPipePeer::eState::FOLLOWING)
                    continue;
                auto data = Log::SRollingLogFollow::get().takeLog(peer->followId());
                if (data.empty())
                    continue;
                if (peer->connection->queued() + data.size() > FOLLOW_QUEUE_LIMIT || !peer->connection->write(std::move(data)))
                    removeLater(peer);
            }
        }

        bool hasFollowers() const {
            return std::ranges::any_of(m_peers, [](const auto& peer) { return !peer->removed && peer->state == CPipePeer::eState::FOLLOWING; });
        }

        // A peer's pipe callbacks are still on the stack; destroy it once they return.
        void removeLater(const SP<CPipePeer>& peer) {
            if (!peer || peer->removed)
                return;
            peer->removed = true;
            g_pEventLoopManager->doLater([this, alive = WP<bool>{m_alive}] {
                if (alive && *alive.lock())
                    std::erase_if(m_peers, [](const auto& p) { return p->removed; });
            });
        }

        FRequestHandler            m_requestHandler;
        UP<CPipeListener>          m_listener;
        std::vector<SP<CPipePeer>> m_peers;
        SP<CEventLoopTimer>        m_followTimer;
        SP<bool>                   m_alive = makeShared<bool>(true);
    };

    UP<IImplementation> makeWindowsImplementation() {
        return makeUnique<CPipeImpl>();
    }
}

// Socket2: events ----------------------------------------------------------------------

namespace IPC::Socket2 {
    // The Unix socket drops a client after 64 unsent events; with byte queues the
    // equivalent bound is on unsent bytes (events are at most ~1 KiB each).
    static constexpr size_t MAX_QUEUED_BYTES = 64 * 1024;

    class CPipeImpl : public IImplementation {
      public:
        CPipeImpl() {
            const auto NAME = hyprlandPipeName(g_pCompositor->m_instanceSignature, ".socket2.sock");
            m_listener      = makeUnique<CPipeListener>(NAME, g_pCompositor->m_wlEventLoop, [this](void* pipe) { onConnection(pipe); });
            if (!m_listener->start()) {
                LOG(Log::ERR, "[Socket2::Pipe] Couldn't start the Hyprland Socket 2. IPC will not work.");
                m_listener.reset();
            }
        }

        ~CPipeImpl() override {
            *m_alive = false;
            m_peers.clear();
            m_listener.reset();
        }

        bool send(std::string&& x) override {
            for (const auto& peer : m_peers) {
                if (peer.removed)
                    continue;
                if (peer.connection->queued() + x.size() > MAX_QUEUED_BYTES) {
                    LOG(Log::ERR, "[Socket2::Pipe] client {} overflowed its event queue, removing", peer.key);
                    removeLater(peer.key);
                    continue;
                }
                if (!peer.connection->write(x))
                    removeLater(peer.key);
            }
            return true;
        }

      private:
        struct SPeer {
            UP<CPipeConnection> connection;
            uint64_t            key     = 0;
            bool                removed = false;
        };

        void onConnection(void* pipe) {
            // Callbacks find their peer by key; clients only listen, so input is discarded.
            const auto KEY        = m_nextKey++;
            auto       connection = makeUnique<CPipeConnection>(pipe, g_pCompositor->m_wlEventLoop,
                                                              CPipeConnection::SCallbacks{
                                                                  .readable = [this, KEY] { discardInput(KEY); },
                                                                  .drained  = [] {},
                                                                  .closed   = [this, KEY] { removeLater(KEY); },
                                                              });
            if (connection->good())
                m_peers.emplace_back(SPeer{.connection = std::move(connection), .key = KEY});
        }

        void discardInput(uint64_t key) {
            for (auto& peer : m_peers) {
                if (peer.key == key)
                    peer.connection->takeInput();
            }
        }

        void removeLater(uint64_t key) {
            for (auto& peer : m_peers) {
                if (peer.key == key)
                    peer.removed = true;
            }
            g_pEventLoopManager->doLater([this, alive = WP<bool>{m_alive}] {
                if (alive && *alive.lock())
                    std::erase_if(m_peers, [](const auto& p) { return p.removed; });
            });
        }

        UP<CPipeListener>  m_listener;
        std::vector<SPeer> m_peers;
        uint64_t           m_nextKey = 1;
        SP<bool>           m_alive   = makeShared<bool>(true);
    };

    UP<IImplementation> makeWindowsImplementation() {
        return makeUnique<CPipeImpl>();
    }
}
