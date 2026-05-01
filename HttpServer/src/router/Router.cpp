#include "../../include/router/Router.h"
#include <algorithm>
#include <muduo/base/Logging.h>
#include <mutex>

namespace http {
namespace router {
namespace {
std::string routeStaticPrefixFromPattern(const std::string &pathPattern) {
  const auto pos = pathPattern.find("/:");
  if (pos == std::string::npos) {
    return "";
  }
  return pathPattern.substr(0, pos + 1);
}

std::vector<std::string> pathPrefixesForLookup(const std::string &path) {
  std::vector<std::string> keys;
  keys.push_back("");
  if (path.empty() || path[0] != '/') {
    return keys;
  }
  for (size_t i = 1; i < path.size(); ++i) {
    if (path[i] == '/') {
      keys.push_back(path.substr(0, i + 1));
    }
  }
  keys.push_back(path);
  std::sort(keys.begin(), keys.end(),
            [](const std::string &a, const std::string &b) {
              return a.size() > b.size();
            });
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  return keys;
}
} // namespace

void Router::registerHandler(HttpRequest::Method method,
                             const std::string &path, HandlerPtr handler) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  RouteKey key{method, path};
  handlers_[key] = std::move(handler);
}

void Router::registerCallback(HttpRequest::Method method,
                              const std::string &path,
                              const HandlerCallback &callback) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  RouteKey key{method, path};
  callbacks_[key] = callback;
}

// 添加正则表达式路由处理器（Handler）
void Router::addRegexHandler(HttpRequest::Method method,
                             const std::string &path, HandlerPtr handler) {
  // 1. 加写锁，保证多线程写安全。
  std::unique_lock<std::shared_mutex> lock(mutex_);
  // 2. 将路径规则字符串转为正则表达式对象，用于后续的路径匹配。
  std::regex pathRegex = convertToRegex(path);
  // 3. 基于请求方法和静态路径前缀组装RegexPrefixKey，做为桶分组的键。
  RegexPrefixKey pkey{method, routeStaticPrefixFromPattern(path)};
  // 4. 把新的(方法, 正则, 处理器)三元组挂到对应桶上，便于高效查找和匹配。
  regexHandlersByPrefix_[pkey].emplace_back(method, std::move(pathRegex),
                                            std::move(handler));
}

// 添加正则表达式路由回调函数（Callback）
void Router::addRegexCallback(HttpRequest::Method method,
                              const std::string &path,
                              const HandlerCallback &callback) {
  // 1. 加写锁，保证多线程写安全。
  std::unique_lock<std::shared_mutex> lock(mutex_);
  // 2. 路径模式转正则，用于动态参数匹配。
  std::regex pathRegex = convertToRegex(path);
  // 3. 路径静态前缀+方法聚合为桶的key。
  RegexPrefixKey pkey{method, routeStaticPrefixFromPattern(path)};
  // 4. (方法, 正则, 回调)入桶，用于路由查找时匹配调用。
  regexCallbacksByPrefix_[pkey].emplace_back(method, std::move(pathRegex),
                                             callback);
}

// 将路径匹配模式字符串转为可匹配的std::regex对象
std::regex Router::convertToRegex(const std::string &pathPattern) {
  // 1. 用正则替换所有"/:xxx"为"/([^/]+)"，捕获类似参数值。
  std::string regexPattern =
      "^" +
      std::regex_replace(pathPattern, std::regex(R"(/:([^/]+))"),
                         R"(/([^/]+))") +
      "$";
  // 2. 前后加^和$保证完全匹配。
  return std::regex(regexPattern);
}

// 从正则匹配结果中提取参数，写入HttpRequest
void Router::extractPathParameters(const std::smatch &match,
                                   HttpRequest &request) {
  // 跳过下标0（整体匹配结果），依次设置param1、param2 等命名参数
  for (size_t i = 1; i < match.size(); ++i) {
    request.setPathParameters("param" + std::to_string(i), match[i].str());
  }
}

bool Router::route(const HttpRequest &req, HttpResponse *resp) {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  RouteKey key{req.method(), req.path()};

  // 精确路径 + 方法：对象式处理器
  auto handlerIt = handlers_.find(key);
  if (handlerIt != handlers_.end()) {
    handlerIt->second->handle(req, resp);
    return true;
  }

  // 精确路径 + 方法：回调式处理器
  auto callbackIt = callbacks_.find(key);
  if (callbackIt != callbacks_.end()) {
    callbackIt->second(req, resp);
    return true;
  }

  const std::string pathStr = req.path();

  for (const std::string &prefix : pathPrefixesForLookup(pathStr)) {
    RegexPrefixKey pkey{req.method(), prefix};
    auto bucket = regexHandlersByPrefix_.find(pkey);
    if (bucket == regexHandlersByPrefix_.end()) {
      continue;
    }
    for (const auto &obj : bucket->second) {
      std::smatch match;
      if (obj.method_ == req.method() &&
          std::regex_match(pathStr, match, obj.pathRegex_)) {
        HttpRequest newReq(req);
        extractPathParameters(match, newReq);
        obj.handler_->handle(newReq, resp);
        return true;
      }
    }
  }

  for (const std::string &prefix : pathPrefixesForLookup(pathStr)) {
    RegexPrefixKey pkey{req.method(), prefix};
    auto bucket = regexCallbacksByPrefix_.find(pkey);
    if (bucket == regexCallbacksByPrefix_.end()) {
      continue;
    }
    for (const auto &obj : bucket->second) {
      std::smatch match;
      if (obj.method_ == req.method() &&
          std::regex_match(pathStr, match, obj.pathRegex_)) {
        HttpRequest newReq(req);
        extractPathParameters(match, newReq);
        obj.callback_(newReq, resp);
        return true;
      }
    }
  }

  return false;
}

} // namespace router
} // namespace http
