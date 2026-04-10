#include "daemon/common.hpp"
#include "observability/error_codes.hpp"
#include "p2p/utils/structured_log.hpp"

#include <gtest/gtest.h>

TEST(Observability, TraceIdUsesPrefixAndIsUnique) {
    const auto first = p2p::utils::make_trace_id("connect");
    const auto second = p2p::utils::make_trace_id("connect");

    EXPECT_NE(first, second);
    EXPECT_EQ(first.rfind("connect-", 0), 0U);
    EXPECT_EQ(second.rfind("connect-", 0), 0U);
}

TEST(Observability, ErrorEnvelopeCarriesDetails) {
    auto response = bto::daemon::make_error(
        bto::observability::code::kDaemonInvalidRequest,
        "target_did is required",
        bto::daemon::Json{{"trace_id", "connect-123"}}
    );

    ASSERT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"].value("code", ""), bto::observability::code::kDaemonInvalidRequest);
    EXPECT_EQ(response["error"].value("message", ""), "target_did is required");
    ASSERT_TRUE(response["error"].contains("details"));
    EXPECT_EQ(response["error"]["details"].value("trace_id", ""), "connect-123");
}

TEST(Observability, StructuredEventIncludesStableFields) {
    auto event = p2p::utils::make_structured_event("peerlinkd", "session", "session.create.ready");
    p2p::utils::put_if_not_empty(event, "trace_id", "connect-abc");

    EXPECT_EQ(event.value("service", ""), "peerlinkd");
    EXPECT_EQ(event.value("component", ""), "session");
    EXPECT_EQ(event.value("event", ""), "session.create.ready");
    EXPECT_EQ(event.value("trace_id", ""), "connect-abc");
    EXPECT_FALSE(event.value("ts", "").empty());
}
