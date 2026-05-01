#pragma once

#include <memory>
#include <shared_mutex>
#include <vector>

#include "Middleware.h"

namespace http 
{
namespace middleware 
{

class MiddlewareChain 
{
public:
    void addMiddleware(std::shared_ptr<Middleware> middleware);
    void processBefore(HttpRequest& request);
    void processAfter(HttpResponse& response);

private:
    std::vector<std::shared_ptr<Middleware>> middlewares_;
    mutable std::shared_mutex mutex_;
};

} // namespace middleware
} // namespace http