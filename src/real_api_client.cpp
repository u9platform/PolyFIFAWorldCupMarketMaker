#include "real_api_client.h"
#include <spdlog/spdlog.h>
#include <curl/curl.h>
#include <stdexcept>

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
}

RealApiClient::~RealApiClient() {
    curl_global_cleanup();
}

std::string RealApiClient::httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw ApiError("Failed to init curl");
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    // Set headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

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
        auto j = nlohmann::json::parse(response);
        spdlog::debug("Order book fetched, bids={} asks={}",
                      j["bids"].size(), j["asks"].size());
        return j;
    } catch (const nlohmann::json::parse_error& e) {
        throw ApiError(std::string("JSON parse error: ") + e.what());
    }
}

double RealApiClient::getBalance() {
    // Balance requires authentication. For now, return a placeholder
    // that allows the bot to start in read-only/monitor mode.
    if (api_key_.empty()) {
        spdlog::warn("No API key configured, returning placeholder balance for monitor mode");
        return 1000.0;
    }
    // TODO: implement authenticated balance query
    throw ApiError("Authenticated balance query not yet implemented");
}

std::string RealApiClient::placeOrder(const std::string& token_id, Side side,
                                       double price, double size) {
    throw ApiError("placeOrder not yet implemented - requires API key + wallet signature");
}

void RealApiClient::cancelOrder(const std::string& order_id) {
    throw ApiError("cancelOrder not yet implemented - requires API key + wallet signature");
}

OrderStatus RealApiClient::getOrderStatus(const std::string& order_id) {
    throw ApiError("getOrderStatus not yet implemented - requires API key + wallet signature");
}

double RealApiClient::getFilledQty(const std::string& order_id) {
    throw ApiError("getFilledQty not yet implemented - requires API key + wallet signature");
}

} // namespace mm
