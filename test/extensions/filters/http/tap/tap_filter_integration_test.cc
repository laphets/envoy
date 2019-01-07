#include "envoy/data/tap/v2alpha/http.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TapIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  TapIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}

  void initializeFilter(const std::string& filter_config) {
    config_helper_.addFilter(filter_config);
    initialize();
  }

  const envoy::data::tap::v2alpha::HttpBufferedTrace::Header*
  findHeader(const std::string& key,
             const Protobuf::RepeatedPtrField<envoy::data::tap::v2alpha::HttpBufferedTrace::Header>&
                 headers) {
    for (const auto& header : headers) {
      if (header.key() == key) {
        return &header;
      }
    }

    return nullptr;
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, TapIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

// Verify a basic tap flow using the admin handler.
// fixfix repeated match configs
TEST_P(TapIntegrationTest, AdminBasicFlow) {
  const std::string FILTER_CONFIG =
      R"EOF(
name: envoy.filters.http.tap
config:
  admin_config:
    config_id: test_config_id
)EOF";

  initializeFilter(FILTER_CONFIG);

  // Initial request/response with no tap.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const Http::TestHeaderMapImpl request_headers_tap{{":method", "GET"},
                                                    {":path", "/"},
                                                    {":scheme", "http"},
                                                    {":authority", "host"},
                                                    {"foo", "bar"}};
  IntegrationStreamDecoderPtr decoder = codec_client_->makeHeaderOnlyRequest(request_headers_tap);
  waitForNextUpstreamRequest();
  const Http::TestHeaderMapImpl response_headers_no_tap{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers_no_tap, true);
  decoder->waitForEndStream();

  const std::string admin_request_yaml =
      R"EOF(
config_id: test_config_id
tap_config:
  match_configs:
    - match_id: foo_match_id
      http_match_config:
        request_match_config:
          headers:
            - name: foo
              exact_match: bar
        response_match_config:
          headers:
            - name: bar
              exact_match: baz
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";

  // Setup a tap and disconnect it without any request/response.
  IntegrationCodecClientPtr admin_client_ =
      makeHttpConnection(makeClientConnection(lookupPort("admin")));
  const Http::TestHeaderMapImpl admin_request_headers{
      {":method", "POST"}, {":path", "/tap"}, {":scheme", "http"}, {":authority", "host"}};
  IntegrationStreamDecoderPtr admin_response =
      admin_client_->makeRequestWithBody(admin_request_headers, admin_request_yaml);
  admin_response->waitForHeaders();
  EXPECT_STREQ("200", admin_response->headers().Status()->value().c_str());
  EXPECT_FALSE(admin_response->complete());
  admin_client_->close();
  test_server_->waitForGaugeEq("http.admin.downstream_rq_active", 0);

  // Second request/response with no tap.
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_tap);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers_no_tap, true);
  decoder->waitForEndStream();

  // Setup the tap again and leave it open.
  admin_client_ = makeHttpConnection(makeClientConnection(lookupPort("admin")));
  admin_response = admin_client_->makeRequestWithBody(admin_request_headers, admin_request_yaml);
  admin_response->waitForHeaders();
  EXPECT_STREQ("200", admin_response->headers().Status()->value().c_str());
  EXPECT_FALSE(admin_response->complete());

  // Do a request which should tap, matching on request headers.
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_tap);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers_no_tap, true);
  decoder->waitForEndStream();

  // Wait for the tap message.
  admin_response->waitForBodyData(1);
  envoy::data::tap::v2alpha::HttpBufferedTrace trace;
  MessageUtil::loadFromYaml(admin_response->body(), trace);
  EXPECT_EQ("foo_match_id", trace.match_id());
  EXPECT_EQ(trace.request_headers().size(), 9);
  EXPECT_EQ(trace.response_headers().size(), 5);
  admin_response->clearBody();

  // Do a request which should not tap.
  const Http::TestHeaderMapImpl request_headers_no_tap{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_no_tap);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(response_headers_no_tap, true);
  decoder->waitForEndStream();

  // Do a request which should tap, matching on response headers.
  decoder = codec_client_->makeHeaderOnlyRequest(request_headers_no_tap);
  waitForNextUpstreamRequest();
  const Http::TestHeaderMapImpl response_headers_tap{{":status", "200"}, {"bar", "baz"}};
  upstream_request_->encodeHeaders(response_headers_tap, true);
  decoder->waitForEndStream();

  // Wait for the tap message.
  admin_response->waitForBodyData(1);
  MessageUtil::loadFromYaml(admin_response->body(), trace);
  EXPECT_EQ("foo_match_id", trace.match_id());
  EXPECT_EQ(trace.request_headers().size(), 8);
  EXPECT_EQ("0", findHeader("content-length", trace.request_headers())->value());
  EXPECT_EQ(trace.response_headers().size(), 6);
  EXPECT_NE(nullptr, findHeader("date", trace.response_headers()));
  EXPECT_EQ("baz", findHeader("bar", trace.response_headers())->value());

  admin_client_->close();
  EXPECT_EQ(2UL, test_server_->counter("http.config_test.tap.rq_tapped")->value());
}

} // namespace
} // namespace Envoy
