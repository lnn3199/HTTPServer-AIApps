#pragma once
#include <functional>
#include <memory>
#include <regex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "RouterHandler.h"
#include "../http/HttpRequest.h"
#include "../http/HttpResponse.h"

namespace http
{
namespace router
{

class Router
{
public:
    using HandlerPtr = std::shared_ptr<RouterHandler>;
    using HandlerCallback = std::function<void(const HttpRequest &, HttpResponse *)>;

    struct RouteKey
    {
        HttpRequest::Method method;
        std::string path;

        bool operator==(const RouteKey &other) const
        {
            return method == other.method && path == other.path;
        }
    };

    struct RouteKeyHash
    {
        size_t operator()(const RouteKey &key) const
        {
            size_t methodHash = std::hash<int>{}(static_cast<int>(key.method));
            size_t pathHash = std::hash<std::string>{}(key.path);
            return methodHash * 31 + pathHash;
        }
    };

    using RegexPrefixKey = std::pair<HttpRequest::Method, std::string>;

    struct RegexPrefixKeyHash
    {
        size_t operator()(const RegexPrefixKey &k) const
        {
            return std::hash<int>{}(static_cast<int>(k.first)) ^
                   (std::hash<std::string>{}(k.second) + 0x9e3779b9);
        }
    };

    void registerHandler(HttpRequest::Method method, const std::string &path, HandlerPtr handler);
    void registerCallback(HttpRequest::Method method, const std::string &path,
                          const HandlerCallback &callback);
    void addRegexHandler(HttpRequest::Method method, const std::string &path, HandlerPtr handler);
    void addRegexCallback(HttpRequest::Method method, const std::string &path,
                          const HandlerCallback &callback);

    bool route(const HttpRequest &req, HttpResponse *resp);

private:
    std::regex convertToRegex(const std::string &pathPattern);
    void extractPathParameters(const std::smatch &match, HttpRequest &request);

    struct RouteCallbackObj
    {
        HttpRequest::Method method_;
        std::regex pathRegex_;
        HandlerCallback callback_;
        RouteCallbackObj(HttpRequest::Method method, std::regex pathRegex, const HandlerCallback &callback)
            : method_(method), pathRegex_(std::move(pathRegex)), callback_(callback) {}
    };

    struct RouteHandlerObj
    {
        HttpRequest::Method method_;
        std::regex pathRegex_;
        HandlerPtr handler_;
        RouteHandlerObj(HttpRequest::Method method, std::regex pathRegex, HandlerPtr handler)
            : method_(method), pathRegex_(std::move(pathRegex)), handler_(std::move(handler)) {}
    };

    std::unordered_map<RouteKey, HandlerPtr, RouteKeyHash> handlers_;
    std::unordered_map<RouteKey, HandlerCallback, RouteKeyHash> callbacks_;
    std::unordered_map<RegexPrefixKey, std::vector<RouteHandlerObj>, RegexPrefixKeyHash>
        regexHandlersByPrefix_;
    std::unordered_map<RegexPrefixKey, std::vector<RouteCallbackObj>, RegexPrefixKeyHash>
        regexCallbacksByPrefix_;

    mutable std::shared_mutex mutex_;
};

} // namespace router
} // namespace http
