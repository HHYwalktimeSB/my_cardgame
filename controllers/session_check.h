
#pragma once

#include <drogon/HttpController.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

using namespace drogon;

class MySessionChecker{
    public:
    enum{SessionOk, ErrNotLogin, ErrInvaildToken, ProcessedByOther};
    static int check_session(const HttpRequestPtr &req,int64_t& userid);
    static bool write_session_info(const HttpRequestPtr & req, int64_t userid);
    static bool erase_session_info(const HttpRequestPtr & req);
    private:
    MySessionChecker();
    int64_t get_token(int64_t userid);
    bool remove_mapping(int64_t userid);
    std::atomic<int64_t> token_counter;
    std::mutex mutex_;
    std::unordered_map<int64_t, int64_t> user_token;
    static MySessionChecker checker;
};
