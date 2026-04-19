#include "../../include/router/Router.h"
#include <muduo/base/Logging.h>

namespace http
{
namespace router
{

void Router::registerHandler(HttpRequest::Method method, const std::string &path, HandlerPtr handler)
{
    RouteKey key{method, path};
    handlers_[key] = std::move(handler);
}

void Router::registerCallback(HttpRequest::Method method, const std::string &path, const HandlerCallback &callback)
{
    RouteKey key{method, path};
    callbacks_[key] = std::move(callback);
}

bool Router::route(const HttpRequest &req, HttpResponse *resp)
{
    RouteKey key{req.method(), req.path()};

    // 查找处理器   `
    auto handlerIt = handlers_.find(key);
    if (handlerIt != handlers_.end())
    {
        // handlerIt->second 是一个shared_ptr<RouterHandler>，调用其handle成员函数
        handlerIt->second->handle(req, resp);
        return true;
    }

    // 查找回调函数
    auto callbackIt = callbacks_.find(key);
    if (callbackIt != callbacks_.end())
    {
        callbackIt->second(req, resp);
        return true;
    }

    // 查找动态路由处理器
    for (const auto &[method, pathRegex, handler] : regexHandlers_)
    {
        std::smatch match;
        std::string pathStr(req.path());
        // 如果方法匹配并且动态路由匹配，则执行处理器
        // std::regex_match 是 C++ 标准库 <regex> 中的一个函数，用于用正则表达式匹配整个字符串。
        // 它返回 true 表示整个 pathStr 和 pathRegex 完全匹配，同时会把每个分组（括号内）匹配的内容存储到 match 中。
        if (method == req.method() && std::regex_match(pathStr, match, pathRegex))
        {
            // Extract path parameters and add them to the request
            HttpRequest newReq(req); // 因为这里需要用这一次所以是可以改的
            extractPathParameters(match, newReq);
            
            handler->handle(newReq, resp);
            return true;
        }
    }

    // 查找动态路由回调函数
    for (const auto &[method, pathRegex, callback] : regexCallbacks_)
    {
        std::smatch match;
        std::string pathStr(req.path());
        if (method == req.method() && std::regex_match(pathStr, match, pathRegex))
        {
            // 提取路径参数，这一步是作用在newReq上的，不会更改原始的req
            HttpRequest newReq(req);
            extractPathParameters(match, newReq);
            callback(newReq, resp);
            return true;
        }
    }

    return false;
}

} // namespace router
} // namespace http