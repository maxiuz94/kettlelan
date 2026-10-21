#include "engine.h"

#include <chrono>

#include "log.h"
#include "netcode.h"
#include "session.h"
#include "tun.h"

namespace kettle::gui {

using Clock = std::chrono::steady_clock;

struct Engine::State {
    steam::Session session;
    std::unique_ptr<steam::Network> net;
    std::unique_ptr<platform::Tun> tun;
    EngineStatus status = EngineStatus::Starting;
    Message error;
    bool dirty = true;
    bool hideAddress = true;
    bool protectServices = true;
};

Engine::Engine(SnapshotHandler onSnapshot, EventHandler onEvent)
    : onSnapshot_(std::move(onSnapshot)), onEvent_(std::move(onEvent)), st_(std::make_unique<State>()) {}

Engine::~Engine() { shutdown(); }

void Engine::post(std::function<void()> fn) {
    std::lock_guard lk(queueMutex_);
    queue_.push_back(std::move(fn));
}

void Engine::start() {
    if (running_) return;
    running_ = true;
    post([this] { initialize(); });
    thread_ = std::thread([this] { loop(); });
}

void Engine::retry() {
    post([this] {
        if (st_->status == EngineStatus::SteamMissing || st_->status == EngineStatus::TunError) initialize();
    });
}

void Engine::create(const std::string& name) {
    post([this, name] {
        if (st_->net) st_->net->create(name);
        st_->dirty = true;
    });
}

void Engine::join(const std::string& text, const std::string& nameHint) {
    post([this, text, nameHint] {
        if (!st_->net) return;
        if (const auto code = netcode::parse(text)) st_->net->connect(*code, nameHint);
        else if (onEvent_) onEvent_({Msg::InvalidCode, {}});
        st_->dirty = true;
    });
}

void Engine::leave() {
    post([this] {
        if (st_->net) st_->net->leave();
        st_->dirty = true;
    });
}

void Engine::setHideAddress(bool hide) {
    post([this, hide] {
        st_->hideAddress = hide;
        if (st_->net) st_->net->setHideAddress(hide);
        st_->dirty = true;
    });
}

void Engine::setProtectServices(bool protect) {
    post([this, protect] {
        st_->protectServices = protect;
        if (st_->net) st_->net->setProtectServices(protect);
    });
}

void Engine::initialize() {
    auto& s = *st_;
    s.error = {};
    if (!s.session.initialized()) {
        s.status = EngineStatus::Starting;
        publish();
        if (!s.session.init(s.error)) {
            s.status = EngineStatus::SteamMissing;
            publish();
            return;
        }
        s.net = std::make_unique<steam::Network>(s.session.steamId(), [this](const Message& message) {
            st_->dirty = true;
            if (onEvent_) onEvent_(message);
        });
        s.net->setHideAddress(s.hideAddress);
        s.net->setProtectServices(s.protectServices);
    }
    if (!s.tun) {
        s.status = EngineStatus::Starting;
        publish();
        s.tun = platform::openTun(s.net->selfAddress(), s.error);
        if (!s.tun) {
            s.status = EngineStatus::TunError;
            publish();
            return;
        }
        auto* tun = s.tun.get();
        auto* net = s.net.get();
        net->setPacketHandler([tun](std::span<const uint8_t> p) { tun->write(p); });
        tun->start([net](std::span<const uint8_t> p) { net->sendPacket(p); });
    }
    s.status = EngineStatus::Ready;
    publish();
}

void Engine::publish() {
    auto& s = *st_;
    Snapshot snap;
    snap.status = s.status;
    snap.error = s.error;
    if (s.session.initialized()) snap.personaName = s.session.personaName();
    if (s.net) {
        snap.selfAddress = s.net->selfAddress().toString();
        snap.netState = s.net->state();
        snap.blockedPackets = s.net->blockedPackets();
        if (snap.netState == steam::State::Connected) {
            snap.networkName = s.net->networkName();
            snap.networkCode = s.net->codeText();
            snap.peers = s.net->peers();
        }
    }
    s.dirty = false;
    if (onSnapshot_) onSnapshot_(snap);
}

void Engine::loop() {
    auto nextTick = Clock::now();
    auto nextPublish = Clock::now();
    while (running_) {
        std::deque<std::function<void()>> jobs;
        {
            std::lock_guard lk(queueMutex_);
            jobs.swap(queue_);
        }
        for (auto& job : jobs) job();

        auto& s = *st_;
        if (s.session.initialized()) {
            s.session.runCallbacks();
            if (s.net) s.net->poll();
            const auto now = Clock::now();
            if (now >= nextTick) {
                if (s.net) s.net->tick();
                nextTick = now + std::chrono::milliseconds(500);
            }
            // snapshot every second (ping) or right after a change
            if (s.dirty || now >= nextPublish) {
                publish();
                nextPublish = now + std::chrono::seconds(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

void Engine::shutdown() {
    if (!running_) return;
    std::atomic<bool> done{false};
    post([this, &done] {
        auto& s = *st_;
        if (s.net) s.net->leave();
        if (s.session.initialized()) s.session.runCallbacks();
        if (s.tun) {
            s.tun->stop();
            s.tun.reset();
        }
        // The network object registers Steam callbacks – it must not be destroyed after SteamAPI_Shutdown().
        s.net.reset();
        s.session.shutdown();
        done = true;
    });
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!done && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    running_ = false;
    if (done && thread_.joinable()) thread_.join();
    else if (thread_.joinable()) thread_.detach(); // Steam is hanging – do not wait forever
}

} // namespace kettle::gui
