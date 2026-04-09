#include "upgrade/upgrade_client.hpp"

#include "p2p/core/artifact_transfer.hpp"
#include "p2p/core/p2p_client.hpp"
#include "p2p/protocol/artifact_transfer.hpp"
#include "p2p/protocol/upgrade_control.hpp"
#include "util/io_context_thread.hpp"

#include <boost/asio.hpp>

#include <spdlog/spdlog.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unistd.h>

namespace bto::upgrade {

namespace {

constexpr int kUpgradeControlChannel = 0;

// Upper bound for relay resolve/connect/register (see RelayTransport) and P2P connect handshake.
constexpr auto kP2pInitializeTimeout = std::chrono::seconds(90);
constexpr auto kP2pConnectTimeout = std::chrono::seconds(90);
constexpr auto kP2pSendTimeout = std::chrono::seconds(30);
constexpr auto kApplyStatusResponseTimeout = std::chrono::seconds(3);
constexpr auto kApplyStatusPollInterval = std::chrono::seconds(1);
constexpr auto kApplyReconnectTimeout = std::chrono::seconds(15);

class ControlMailbox {
public:
    void push(const p2p::protocol::UpgradeControlFrame& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        frames_.push_back(frame);
        cv_.notify_all();
    }

    template <typename Predicate>
    auto wait_for(Predicate&& predicate, std::chrono::milliseconds timeout)
        -> std::optional<p2p::protocol::UpgradeControlFrame> {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            for (auto it = frames_.begin(); it != frames_.end(); ++it) {
                if (predicate(*it)) {
                    auto frame = *it;
                    frames_.erase(it);
                    return frame;
                }
            }
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                return std::nullopt;
            }
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<p2p::protocol::UpgradeControlFrame> frames_;
};

struct CallbackWaitState {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool timed_out = false;
    std::error_code result;
};

auto wait_for_callback_timeout(
    std::function<void(std::function<void(const std::error_code&)>)> starter,
    std::chrono::steady_clock::duration timeout,
    std::function<void()> on_timeout = {}) -> std::error_code {
    auto state = std::make_shared<CallbackWaitState>();

    starter([state](const std::error_code& ec) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->done || state->timed_out) {
            return;
        }
        state->done = true;
        state->result = ec;
        state->cv.notify_all();
    });

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(state->mutex);
    while (!state->done) {
        if (state->cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            break;
        }
    }
    if (!state->done) {
        state->timed_out = true;
        lock.unlock();
        if (on_timeout) {
            on_timeout();
        }
        return std::make_error_code(std::errc::timed_out);
    }
    return state->result;
}

}  // namespace

auto run_remote_upgrade(const UpgradeRequest& request) -> UpgradeResult {
    UpgradeResult result;

    p2p::core::P2PConfig cfg;
    cfg.stun_server = "";
    cfg.stun_port = 0;
    cfg.relay_server = request.relay_host;
    cfg.relay_port = request.relay_port;
    cfg.signaling_server = request.relay_host;
    cfg.signaling_port = 8080;
    cfg.tcp_relay_server = request.relay_host;
    cfg.tcp_relay_port = request.relay_port;
    cfg.relay_mode = p2p::core::RelayMode::RELAY_ONLY;

    bto::util::IoContextThread io_runner;
    auto& io_context = io_runner.context();
    spdlog::info("[bto upgrade] P2P io worker started");
    auto client = std::make_shared<p2p::core::P2PClient>(io_context, request.local_did, cfg);
    auto mailbox = std::make_shared<ControlMailbox>();

    client->on_data([mailbox](int channel_id, const std::vector<uint8_t>& data) {
        if (channel_id != kUpgradeControlChannel) {
            return;
        }
        auto frame = p2p::protocol::decode_upgrade_control_frame(data);
        if (frame.has_value()) {
            mailbox->push(*frame);
        }
    });

    auto stop_io = [&]() {
        io_runner.shutdown();
        spdlog::info("[bto upgrade] P2P io worker stopped");
    };

    auto init_ec = wait_for_callback_timeout(
        [&](auto cb) { client->initialize(std::move(cb)); },
        kP2pInitializeTimeout,
        [&]() { client->close(); });
    if (init_ec) {
        result.error = init_ec == std::errc::timed_out
            ? std::string("timed out waiting for P2P initialize (relay register)")
            : init_ec.message();
        client->close();
        stop_io();
        return result;
    }

    auto connect_ec = wait_for_callback_timeout(
        [&](auto cb) { client->connect(request.target_did, std::move(cb)); },
        kP2pConnectTimeout,
        [&]() { client->close(); });
    if (connect_ec) {
        result.error = connect_ec == std::errc::timed_out
            ? std::string("timed out waiting for P2P connect to peer")
            : connect_ec.message();
        client->close();
        stop_io();
        return result;
    }

    p2p::protocol::ArtifactOffer offer;
    offer.transfer_id = "upgrade-" + request.artifact.name + "-" + std::to_string(::getpid());
    offer.artifact_name = request.artifact.name;
    offer.artifact_type = "binary";
    offer.version = "latest";
    offer.sender_did = request.local_did;
    offer.receiver_did = request.target_did;
    offer.sha256 = request.artifact.sha256;
    offer.total_size = request.artifact.size;
    offer.chunk_size = 256 * 1024;

    p2p::core::ArtifactSender sender(request.artifact_path, offer);
    if (!sender.open()) {
        result.error = "failed to open artifact";
        client->close();
        stop_io();
        return result;
    }

    const auto artifact_channel = client->create_channel();
    const auto offer_frame = p2p::protocol::UpgradeOfferRequest{offer, artifact_channel};
    const auto offer_payload = p2p::protocol::encode_upgrade_control_frame(offer_frame);
    auto send_offer_ec = wait_for_callback_timeout(
        [&](auto cb) { client->send_data(kUpgradeControlChannel, offer_payload, std::move(cb)); },
        kP2pSendTimeout,
        [&]() { client->close(); });
    if (send_offer_ec) {
        result.error = send_offer_ec == std::errc::timed_out
            ? std::string("timed out sending upgrade offer")
            : send_offer_ec.message();
        client->close();
        stop_io();
        return result;
    }

    auto offer_response = mailbox->wait_for(
        [&](const auto& frame) {
            return std::holds_alternative<p2p::protocol::UpgradeOfferResponse>(frame) &&
                   std::get<p2p::protocol::UpgradeOfferResponse>(frame).transfer_id == offer.transfer_id;
        },
        std::chrono::seconds(10));
    if (!offer_response.has_value()) {
        result.error = "timed out waiting for offer response";
        client->close();
        stop_io();
        return result;
    }

    const auto& accepted = std::get<p2p::protocol::UpgradeOfferResponse>(*offer_response);
    if (!accepted.accepted) {
        result.error = accepted.error;
        client->close();
        stop_io();
        return result;
    }

    if (!sender.resume_from(accepted.resume_offset)) {
        result.error = "failed to resume artifact sender";
        client->close();
        stop_io();
        return result;
    }

    while (auto chunk = sender.next_chunk()) {
        const auto next_offset = chunk->offset + chunk->data.size();
        const auto payload = p2p::protocol::encode_artifact_frame(*chunk);
        auto send_chunk_ec = wait_for_callback_timeout(
            [&](auto cb) { client->send_data(artifact_channel, payload, std::move(cb)); },
            kP2pSendTimeout,
            [&]() { client->close(); });
        if (send_chunk_ec) {
            result.error = send_chunk_ec == std::errc::timed_out
                ? std::string("timed out sending artifact chunk")
                : send_chunk_ec.message();
            client->close();
            stop_io();
            return result;
        }

        auto status = mailbox->wait_for(
            [&](const auto& frame) {
                return std::holds_alternative<p2p::protocol::UpgradeStatusResponse>(frame) &&
                       std::get<p2p::protocol::UpgradeStatusResponse>(frame).transfer_id == offer.transfer_id &&
                       std::get<p2p::protocol::UpgradeStatusResponse>(frame).committed_offset >= next_offset;
            },
            std::chrono::seconds(10));
        if (!status.has_value()) {
            result.error = "timed out waiting for chunk ack";
            client->close();
            stop_io();
            return result;
        }
        const auto& status_frame = std::get<p2p::protocol::UpgradeStatusResponse>(*status);
        if (!status_frame.error.empty()) {
            result.error = status_frame.error;
            client->close();
            stop_io();
            return result;
        }
    }

    const auto complete_payload = p2p::protocol::encode_artifact_frame(sender.complete_frame());
    auto send_complete_ec = wait_for_callback_timeout(
        [&](auto cb) { client->send_data(artifact_channel, complete_payload, std::move(cb)); },
        kP2pSendTimeout,
        [&]() { client->close(); });
    if (send_complete_ec) {
        result.error = send_complete_ec == std::errc::timed_out
            ? std::string("timed out sending transfer completion")
            : send_complete_ec.message();
        client->close();
        stop_io();
        return result;
    }

    auto completion = mailbox->wait_for(
        [&](const auto& frame) {
            return std::holds_alternative<p2p::protocol::UpgradeStatusResponse>(frame) &&
                   std::get<p2p::protocol::UpgradeStatusResponse>(frame).transfer_id == offer.transfer_id &&
                   std::get<p2p::protocol::UpgradeStatusResponse>(frame).transfer_complete;
        },
        std::chrono::seconds(10));
    if (!completion.has_value()) {
        result.error = "timed out waiting for transfer completion";
        client->close();
        stop_io();
        return result;
    }

    const p2p::protocol::UpgradeApplyRequest apply_request{
        offer.transfer_id,
        request.live_binary,
        request.activate_command,
        request.rollback_command,
        request.health_command,
        request.timeout_seconds,
    };
    const auto apply_payload = p2p::protocol::encode_upgrade_control_frame(apply_request);
    auto send_apply_ec = wait_for_callback_timeout(
        [&](auto cb) { client->send_data(kUpgradeControlChannel, apply_payload, std::move(cb)); },
        kP2pSendTimeout,
        [&]() { client->close(); });
    if (send_apply_ec) {
        result.error = send_apply_ec == std::errc::timed_out
            ? std::string("timed out sending apply request")
            : send_apply_ec.message();
        client->close();
        stop_io();
        return result;
    }

    auto apply_response = mailbox->wait_for(
        [&](const auto& frame) {
            return std::holds_alternative<p2p::protocol::UpgradeApplyResponse>(frame) &&
                   std::get<p2p::protocol::UpgradeApplyResponse>(frame).transfer_id == offer.transfer_id;
        },
        std::chrono::seconds(10));
    if (apply_response.has_value()) {
        const auto& apply = std::get<p2p::protocol::UpgradeApplyResponse>(*apply_response);
        if (!apply.accepted) {
            result.error = apply.error.empty() ? "apply request rejected" : apply.error;
            client->close();
            stop_io();
            return result;
        }
    }

    const auto status_payload = p2p::protocol::encode_upgrade_control_frame(
        p2p::protocol::UpgradeStatusRequest{offer.transfer_id});
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(request.timeout_seconds + 30);
    while (std::chrono::steady_clock::now() < deadline) {
        auto send_status_ec = wait_for_callback_timeout(
            [&](auto cb) { client->send_data(kUpgradeControlChannel, status_payload, std::move(cb)); },
            kP2pSendTimeout);
        if (send_status_ec) {
            (void)wait_for_callback_timeout(
                [&](auto cb) { client->connect(request.target_did, std::move(cb)); },
                kApplyReconnectTimeout);
            std::this_thread::sleep_for(kApplyStatusPollInterval);
            continue;
        }

        auto status_response = mailbox->wait_for(
            [&](const auto& frame) {
                return std::holds_alternative<p2p::protocol::UpgradeStatusResponse>(frame) &&
                       std::get<p2p::protocol::UpgradeStatusResponse>(frame).transfer_id ==
                           offer.transfer_id &&
                       (std::get<p2p::protocol::UpgradeStatusResponse>(frame).apply_started ||
                        std::get<p2p::protocol::UpgradeStatusResponse>(frame).apply_finished);
            },
            kApplyStatusResponseTimeout);
        if (status_response.has_value()) {
            const auto& status = std::get<p2p::protocol::UpgradeStatusResponse>(*status_response);
            if (status.apply_finished) {
                result.success = status.apply_success;
                result.replaced = status.replaced;
                result.rolled_back = status.rolled_back;
                result.error = status.error;
                client->close();
                stop_io();
                return result;
            }
        }
        std::this_thread::sleep_for(kApplyStatusPollInterval);
    }

    result.error = apply_response.has_value()
        ? "timed out waiting for apply completion status"
        : "timed out waiting for apply acceptance or completion status";

    client->close();
    stop_io();
    return result;
}

}  // namespace bto::upgrade
