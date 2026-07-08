#include "session_check.h"

MySessionChecker MySessionChecker::checker;

int MySessionChecker::check_session(const HttpRequestPtr &req,int64_t& userid)
{
    auto userId = req->session()->getOptional<int64_t>("user_id");
    auto token = req->session()->getOptional<int64_t>("req_token");
    if(userId && token){
        std::lock_guard<std::mutex> guard(checker.mutex_);
        auto record_tok = checker.user_token.find(*userId);
        if(record_tok != checker.user_token.end()){
            if(record_tok->second==*token){
                userid = *userId;
                return SessionOk;
            }else return ErrInvaildToken;
        }
        else return ErrInvaildToken;
    }return ErrNotLogin;
}

bool MySessionChecker::write_session_info(const HttpRequestPtr &req, int64_t userid)
{
    auto tok = checker.get_token(userid);
    if(tok==-1)return false;
    req->session()->insert("user_id", userid);
    req->session()->insert("req_token", tok);
    return true;
}

bool MySessionChecker::erase_session_info(const HttpRequestPtr &req)
{
    auto userId = req->session()->getOptional<int64_t>("user_id");
    auto token = req->session()->getOptional<int64_t>("req_token");
    if(userId && token){
        req->session()->erase("user_id");
        req->session()->erase("req_token");
        return checker.remove_mapping(*userId);
    }
    return false;
}

int64_t MySessionChecker::get_token(int64_t userid)
{
    int64_t tok = token_counter.fetch_add(1);
    std::lock_guard<std::mutex> guard(mutex_);
    user_token[userid] = tok;
    return tok;
}

bool MySessionChecker::remove_mapping(int64_t userid)
{
    std::lock_guard<std::mutex> guard(mutex_);
    return user_token.erase(userid) > 0;
}

MySessionChecker::MySessionChecker(){
    token_counter = 0;
    
}
