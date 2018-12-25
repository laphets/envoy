#pragma once

#include "envoy/http/header_map.h"

#include "common/common/logger.h"
#include "common/http/header_utility.h"

#include "extensions/common/tap/tap_config_base.h"
#include "extensions/filters/http/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

class HttpTapConfigImpl : Extensions::Common::Tap::TapConfigBaseImpl,
                          public HttpTapConfig,
                          public std::enable_shared_from_this<HttpTapConfigImpl> {
public:
  HttpTapConfigImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                    Extensions::Common::Tap::Sink* admin_streamer);

  absl::string_view matchesRequestHeaders(const Http::HeaderMap& headers);
  absl::string_view matchesResponseHeaders(const Http::HeaderMap& headers);
  Extensions::Common::Tap::Sink& sink() {
    // TODO(mattklein123): When we support multiple sinks, select the right one. Right now
    // it must be admin.
    return *admin_streamer_;
  }

  // TapFilter::HttpTapConfig
  HttpPerRequestTapperPtr newPerRequestTapper() override;

private:
  struct MatchConfig {
    std::string match_id_;
    std::vector<Http::HeaderUtility::HeaderData> request_headers_to_match_;
    std::vector<Http::HeaderUtility::HeaderData> response_headers_to_match_;
  };

  Extensions::Common::Tap::Sink* admin_streamer_;
  std::vector<MatchConfig> match_configs_;
};

typedef std::shared_ptr<HttpTapConfigImpl> HttpTapConfigImplSharedPtr;

class HttpPerRequestTapperImpl : public HttpPerRequestTapper, Logger::Loggable<Logger::Id::tap> {
public:
  HttpPerRequestTapperImpl(HttpTapConfigImplSharedPtr config) : config_(config) {}

  // TapFilter::HttpPerRequestTapper
  void onRequestHeaders(const Http::HeaderMap& headers) override;
  void onResponseHeaders(const Http::HeaderMap& headers) override;
  bool onLog(const Http::HeaderMap* request_headers,
             const Http::HeaderMap* response_headers) override;

private:
  HttpTapConfigImplSharedPtr config_;
  absl::string_view match_id_;
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
