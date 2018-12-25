#include "extensions/filters/http/tap/tap_config_impl.h"

#include "envoy/data/tap/v2alpha/http.pb.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

HttpTapConfigImpl::HttpTapConfigImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                                     Common::Tap::Sink* admin_streamer)
    : Extensions::Common::Tap::TapConfigBaseImpl(std::move(proto_config)),
      admin_streamer_(admin_streamer) {
  // TODO(mattklein123): The streaming admin output sink is the only currently supported sink.
  ASSERT(admin_streamer);
  ASSERT(proto_config_.output_config().sinks()[0].has_streaming_admin());

  for (const auto& proto_match_config : proto_config_.match_configs()) {
    match_configs_.emplace_back();
    MatchConfig& match_config = match_configs_.back();
    match_config.match_id_ = proto_match_config.match_id();

    for (const auto& header_match :
         proto_match_config.http_match_config().request_match_config().headers()) {
      match_config.request_headers_to_match_.emplace_back(header_match);
    }

    for (const auto& header_match :
         proto_match_config.http_match_config().response_match_config().headers()) {
      match_config.response_headers_to_match_.emplace_back(header_match);
    }
  }
}

absl::string_view HttpTapConfigImpl::matchesRequestHeaders(const Http::HeaderMap& headers) {
  for (const MatchConfig& match_config : match_configs_) {
    if (!match_config.request_headers_to_match_.empty() &&
        Http::HeaderUtility::matchHeaders(headers, match_config.request_headers_to_match_)) {
      return match_config.match_id_;
    }
  }

  return absl::string_view();
}

absl::string_view HttpTapConfigImpl::matchesResponseHeaders(const Http::HeaderMap& headers) {
  for (const MatchConfig& match_config : match_configs_) {
    if (!match_config.response_headers_to_match_.empty() &&
        Http::HeaderUtility::matchHeaders(headers, match_config.response_headers_to_match_)) {
      return match_config.match_id_;
    }
  }

  return absl::string_view();
}

HttpPerRequestTapperPtr HttpTapConfigImpl::newPerRequestTapper() {
  return std::make_unique<HttpPerRequestTapperImpl>(shared_from_this());
}

void HttpPerRequestTapperImpl::onRequestHeaders(const Http::HeaderMap& headers) {
  match_id_ = config_->matchesRequestHeaders(headers);
  if (!match_id_.empty()) {
    ENVOY_LOG(debug, "matches request headers");
  }
}

void HttpPerRequestTapperImpl::onResponseHeaders(const Http::HeaderMap& headers) {
  if (match_id_.empty()) {
    match_id_ = config_->matchesResponseHeaders(headers);
    if (!match_id_.empty()) {
      ENVOY_LOG(debug, "matches response headers");
    }
  }
}

bool HttpPerRequestTapperImpl::onLog(const Http::HeaderMap* request_headers,
                                     const Http::HeaderMap* response_headers) {
  if (match_id_.empty()) {
    return false;
  }

  auto trace = std::make_shared<envoy::data::tap::v2alpha::HttpBufferedTrace>();
  trace->set_match_id(std::string(match_id_));
  request_headers->iterate(
      [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
        envoy::data::tap::v2alpha::HttpBufferedTrace& trace =
            *reinterpret_cast<envoy::data::tap::v2alpha::HttpBufferedTrace*>(context);
        auto& new_header = *trace.add_request_headers();
        new_header.set_key(header.key().c_str());
        new_header.set_value(header.value().c_str());
        return Http::HeaderMap::Iterate::Continue;
      },
      trace.get());
  if (response_headers != nullptr) {
    response_headers->iterate(
        [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
          envoy::data::tap::v2alpha::HttpBufferedTrace& trace =
              *reinterpret_cast<envoy::data::tap::v2alpha::HttpBufferedTrace*>(context);
          auto& new_header = *trace.add_response_headers();
          new_header.set_key(header.key().c_str());
          new_header.set_value(header.value().c_str());
          return Http::HeaderMap::Iterate::Continue;
        },
        trace.get());
  }

  ENVOY_LOG(debug, "submitting buffered trace sink");
  config_->sink().submitBufferedTrace(trace);
  return true;
}

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
