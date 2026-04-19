#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>

namespace http
{

namespace session
{

class SessionManager;

// 继承 std::enable_shared_from_this 是为了能够在成员函数内部安全地获取当前对象的 shared_ptr 实例，
// 以便在需要时将this指针安全地传递给SessionManager等其他管理者，防止对象被提前析构或悬空。
class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(const std::string& sessionId, SessionManager* sessionManager, int maxAge = 3600); // 默认1小时过期
    
    const std::string& getId() const 
    { return sessionId_; }

    bool isExpired() const;
    void refresh(); // 刷新过期时间

    void setManager(SessionManager* sessionManager) 
    { sessionManager_ = sessionManager; }

    SessionManager* getManager() const 
    { return sessionManager_; }

    // 数据存取
    void setValue(const std::string&key, const std::string&value);
    std::string getValue(const std::string&key) const;
    void remove(const std::string&key);
    void clear();
private:
    std::string                                  sessionId_;
    std::unordered_map<std::string, std::string> data_;
    std::chrono::system_clock::time_point        expiryTime_;//绝对过期时间点
    int                                          maxAge_; // 过期时间（秒）
    SessionManager*                              sessionManager_;
};

} // namespace session
} // namespace http