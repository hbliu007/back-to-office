#include "upgrade/upgrade_client.hpp"

#include "p2p/core/artifact_transfer.hpp"
#include "p2p/core/p2p_client.hpp"
#include "p2p/protocol/artifact_transfer.hpp"
#include "p2p/protocol/upgrade_control.hpp"

#include <boost/asio.hpp>

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

auto wait_for_callback(std::function<void(std::function<void(const std::error_code&)>)> starter)
    -> std::error_code {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    std::error_code result;

    starter([&](const std::error_code& ec) {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
        result = ec;
        cv.notify_all();
    });

    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&]() { return done; });
    return result;
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

    boost::asio::io_context io_context;
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

    std::thread io_thread([&]() { io_context.run(); });

    auto init_ec = wait_for_callback([&](auto cb) { client->initialize(std::move(cb)); });
    if (init_ec) {
        result.error = init_ec.message();
        io_context.stop();
        io_thread.join();
        return result;
    }

    auto connect_ec = wait_for_callback([&](auto cb) { client->connect(request.target_did, std::move(cb)); });
    if (connect_ec) {
        result.error = connect_ec.message();
        io_context.stop();
        io_thread.join();
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
        io_context.stop();
        io_thread.join();
        return result;
    }

    const auto artifact_channel = client->create_channel();
    const auto offer_frame = p2p::protocol::UpgradeOfferRequest{offer, artifact_channel};
    const auto offer_payload = p2p::protocol::encode_upgrade_control_frame(offer_frame);
    auto send_offer_ec = wait_for_callback([&](auto cb) {
        client->send_data(kUpgradeControlChannel, offer_payload, std::move(cb));
    });
    if (send_offer_ec) {
        result.error = send_offer_ec.message();
        client->close();
        io_context.stop();
        io_thread.join();
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
        io_context.stop();
        io_thread.join();
        return result;
    }

    const auto& accepted = std::get<p2p::protocol::UpgradeOfferResponse>(*offer_response);
    if (!accepted.accepted) {
        result.error = accepted.error;
        client->close();
        io_context.stop();
        io_thread.join();
        return result;
    }

    if (!sender.resume_from(accepted.resume_offset)) {
        result.error = "failed to resume artifact sender";
        client->close();
        io_context.stop();
        io_thread.join();
        return result;
    }

    while (auto chunk = sender.next_chunk()) {
        const auto next_offset = chunk->offset + chunk->data.size();
        const auto payload = p2p::protocol::encode_artifact_frame(*chunk);
        auto send_chunk_ec = wait_for_callback([&](auto cb) {
            client->send_data(artifact_channel, payload, std::move(cb));
        });
        if (send_chunk_ec) {
            result.error = send_chunk_ec.message();
            client->close();
            io_context.stop();
            io_thread.join();
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
            io_context.stop();
            io_thread.join();
            return result;
        }
        const auto& status_frame = std::get<p2p::protocol::UpgradeStatusResponse>(*status);
        if (!status_frame.error.empty()) {
            result.error = status_frame.error;
            client->close();
            io_context.stop();
            io_thread.join();
            return result;
        }
    }

    const auto complete_payload = p2p::protocol::encode_artifact_frame(sender.complete_frame());
    auto send_complete_ec = wait_for_callback([&](auto cb) {
        client->send_data(artifact_channel, complete_payload, std::move(cb));
    });
    if (send_complete_ec) {
        result.error = send_complete_ec.message();
        client->close();
        io_context.stop();
        io_thread.join();
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
        io_context.stop();
        io_thread.join();
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
    auto send_apply_ec = wait_for_callback([&](auto cb) {
        client->send_data(kUpgradeControlChannel, apply_payload, std::move(cb));
    });
    if (send_apply_ec) {
        result.error = send_apply_ec.message();
        client->close();
        io_context.stop();
        io_thread.join();
        return result;
    }

    auto apply_response = mailbox->wait_for(
        [&](const auto& frame) {
            return std::holds_alternative<p2p::protocol::UpgradeApplyResponse>(frame) &&
                   std::get<p2p::protocol::UpgradeApplyResponse>(frame).transfer_id == offer.transfer_id;
        },
        std::chrono::seconds(request.timeout_seconds + 10));
    if (!apply_response.has_value()) {
        result.error = "timed out waiting for apply response";
        client->close();
        io_context.stop();
        io_thread.join();
        return result;
    }

    const auto& apply = std::get<p2p::protocol::UpgradeApplyResponse>(*apply_response);
    result.success = apply.success;
    result.replaced = apply.replaced;
    result.rolled_back = apply.rolled_back;
    result.error = apply.error;

    client->close();
    io_context.stop();
    io_thread.join();
    return result;
}

}  // namespace bto::upgrade
