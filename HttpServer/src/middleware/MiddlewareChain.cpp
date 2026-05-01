#include "../../include/middleware/MiddlewareChain.h"
#include <muduo/base/Logging.h>
#include <mutex>

namespace http
{
namespace middleware
{

void MiddlewareChain::addMiddleware(std::shared_ptr<Middleware> middleware)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    middlewares_.push_back(std::move(middleware));
}

void MiddlewareChain::processBefore(HttpRequest &request)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (auto &middleware : middlewares_)
    {
        middleware->before(request);
    }
}

void MiddlewareChain::processAfter(HttpResponse &response)
{
    try
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        // 反向处理响应，以保持中间件的正确执行顺序
        for (auto it = middlewares_.rbegin(); it != middlewares_.rend(); ++it)
        {
            if (*it)
            { // 添加空指针检查
                (*it)->after(response);
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Error in middleware after processing: " << e.what();
    }
}

} // namespace middleware
} // namespace http
