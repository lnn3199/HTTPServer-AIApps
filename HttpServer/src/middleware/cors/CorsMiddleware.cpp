#include "../../../include/middleware/cors/CorsMiddleware.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <muduo/base/Logging.h>

namespace http 
{
namespace middleware 
{

CorsMiddleware::CorsMiddleware(const CorsConfig& config) : config_(config) {}

void CorsMiddleware::before(HttpRequest& request) 
{
    LOG_DEBUG << "CorsMiddleware::before - Processing request";
    requestOrigin_ = request.getHeader("Origin");
    
    if (request.method() == HttpRequest::Method::kOptions) 
    {
        LOG_INFO << "Processing CORS preflight request";
        HttpResponse response;
        handlePreflightRequest(request, response);
        throw response;
    }
}

void CorsMiddleware::after(HttpResponse& response) 
{
    LOG_DEBUG << "CorsMiddleware::after - Processing response";

    const std::string allowOrigin = resolveAllowOrigin(requestOrigin_);
    if (allowOrigin.empty())
    {
        return;
    }

    addCorsHeaders(response, allowOrigin);
    if (allowOrigin != "*")
    {
        response.addHeader("Vary", "Origin");
    }
}

bool CorsMiddleware::isOriginAllowed(const std::string& origin) const 
{
    return config_.allowedOrigins.empty() || 
           std::find(config_.allowedOrigins.begin(), 
                    config_.allowedOrigins.end(), "*") != config_.allowedOrigins.end() ||
           std::find(config_.allowedOrigins.begin(), 
                    config_.allowedOrigins.end(), origin) != config_.allowedOrigins.end();
}

std::string CorsMiddleware::resolveAllowOrigin(const std::string& requestOrigin) const
{
    if (requestOrigin.empty() || !isOriginAllowed(requestOrigin))
    {
        return "";
    }

    const bool wildcardAllowed =
        std::find(config_.allowedOrigins.begin(), config_.allowedOrigins.end(), "*") != config_.allowedOrigins.end();

    if (wildcardAllowed && !config_.allowCredentials)
    {
        return "*";
    }

    return requestOrigin;
}

// 该方法用于处理 CORS 跨域“预检请求”(Preflight Request)。预检请求是浏览器针对跨域 HTTP 请求，先发送一个 OPTIONS 方法、带有特殊头部的请求（如 Origin、Access-Control-Request-Method、Access-Control-Request-Headers）以获知服务器是否允许实际的跨域操作。

void CorsMiddleware::handlePreflightRequest(const HttpRequest& request, 
                                          HttpResponse& response) 
{
    const std::string& origin = request.getHeader("Origin");

    const std::string allowOrigin = resolveAllowOrigin(origin);
    if (allowOrigin.empty())
    {
        LOG_WARN << "Origin not allowed: " << origin;
        response.setStatusCode(HttpResponse::k403Forbidden);
        return;
    }

    addCorsHeaders(response, allowOrigin);
    if (allowOrigin != "*")
    {
        response.addHeader("Vary", "Origin");
    }
    response.setStatusCode(HttpResponse::k204NoContent);
    LOG_INFO << "Preflight request processed successfully";
}

void CorsMiddleware::addCorsHeaders(HttpResponse& response, 
                                  const std::string& origin) 
{
    try 
    {
        response.addHeader("Access-Control-Allow-Origin", origin);
        
        if (config_.allowCredentials) 
        {
            response.addHeader("Access-Control-Allow-Credentials", "true");
        }
        
        if (!config_.allowedMethods.empty()) 
        {
            response.addHeader("Access-Control-Allow-Methods", 
                             join(config_.allowedMethods, ", "));
        }
        
        if (!config_.allowedHeaders.empty()) 
        {
            response.addHeader("Access-Control-Allow-Headers", 
                             join(config_.allowedHeaders, ", "));
        }
        
        response.addHeader("Access-Control-Max-Age", 
                          std::to_string(config_.maxAge));
        
        LOG_DEBUG << "CORS headers added successfully";
    } 
    catch (const std::exception& e) 
    {
        LOG_ERROR << "Error adding CORS headers: " << e.what();
    }
}

// 工具函数：将字符串数组连接成单个字符串
std::string CorsMiddleware::join(const std::vector<std::string>& strings, const std::string& delimiter) 
{
    std::ostringstream result;
    for (size_t i = 0; i < strings.size(); ++i) 
    {
        if (i > 0) result << delimiter;
        result << strings[i];
    }
    return result.str();
}

} // namespace middleware
} // namespace http