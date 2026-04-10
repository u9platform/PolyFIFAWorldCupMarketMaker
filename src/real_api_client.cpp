#include "real_api_client.h"
#include <spdlog/spdlog.h>
#include <curl/curl.h>

namespace mm {

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* out) {
    size_t total = size * nmemb;
    out->append(static_cast<char*>(contents), total);
    return total;
}

RealApiClient::RealApiClient(const std::string& api_key,
                             const std::string& api_secret,
                             const std::string& private_key)
    : api_key_(api_key), api_secret_(api_secret), private_key_(private_key) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl_ = curl_easy_init();
    if (!curl_) {
        throw ApiError("Failed to init curl handle");
    }

    // Persistent settings on reused handle
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl_, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);

    headers_ = curl_slist_append(nullptr, "Accept: application/json");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
}

RealApiClient::~RealApiClient() {
    if (headers_) curl_slist_free_all(headers_);
    if (curl_) curl_easy_cleanup(curl_);
    curl_global_cleanup();
}

std::string RealApiClient::httpGet(const std::string& url) {
    std::lock_guard<std::mutex> lock(curl_mutex_);

    std::string response;
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl_);

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        throw ApiError(std::string("HTTP request failed: ") + curl_easy_strerror(res));
    }
    if (http_code != 200) {
        throw ApiError("HTTP " + std::to_string(http_code) + ": " + response);
    }

    return response;
}

nlohmann::json RealApiClient::getOrderBook(const std::string& token_id) {
    std::string url = base_url_ + "/book?token_id=" + token_id;
    std::string response = httpGet(url);

    try {
        return nlohmann::json::parse(response);
    } catch (const nlohmann::json::parse_error& e) {
        throw ApiError(std::string("JSON parse error: ") + e.what());
    }
}

double RealApiClient::getBalance() {
    if (api_key_.empty()) {
        return 1000.0;  // placeholder for dryrun/monitor
    }
    throw ApiError("Authenticated balance query not yet implemented");
}

std::string RealApiClient::placeOrder(const std::string& token_id, Side side,
                                       double price, double size) {
    throw ApiError("placeOrder not yet implemented");
}

void RealApiClient::cancelOrder(const std::string& order_id) {
    throw ApiError("cancelOrder not yet implemented");
}

OrderStatus RealApiClient::getOrderStatus(const std::string& order_id) {
    throw ApiError("getOrderStatus not yet implemented");
}

double RealApiClient::getFilledQty(const std::string& order_id) {
    throw ApiError("getFilledQty not yet implemented");
}

} // namespace mm
