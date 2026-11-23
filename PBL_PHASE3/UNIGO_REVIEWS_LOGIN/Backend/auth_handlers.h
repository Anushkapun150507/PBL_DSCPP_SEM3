// auth_handlers.h -- sign up and log in for the whole UNIGO site (the accounts themselves are in auth_store.h).
//
//   POST /api/auth/signup  {"name","roll","password","confirm"}   -> new account + logged in
//   POST /api/auth/login   {"roll","password"}                     -> logged in
//   POST /api/auth/logout                                          -> logged out
//   GET  /api/auth/me                                              -> who is logged in
//   POST /api/auth/name    {"name"}                                -> save the name of an older account
//
// The login is a random token in an HttpOnly, SameSite=Strict cookie, so the page's JavaScript can't read it
// and other websites can't use it. Writes accept JSON only.
#pragma once
#include <ctime>
#include <string>
#include "auth_store.h"
#include "http.h"
#include "json_reader.h"
#include "json_writer.h"

namespace unigo {

// Shared by every handler that needs to know who is logged in (login handlers, Lost & Found handlers).
class AuthHandlerBase : public RequestHandler {
public:
    explicit AuthHandlerBase(AuthStore& auth) : auth_(auth) {}

protected:
    static long long now() { return (long long)std::time(nullptr); }
    const LfUser* user(const HttpRequest& req) const { return auth_.userForToken(req.cookie("lf_session"), now()); }

    static HttpResponse fieldError(const std::string& field, const std::string& message, int status = 400) {
        JsonWriter j;
        j.beginObject().key("error").str("invalid").key("field").str(field).key("message").str(message).endObject();
        return HttpResponse::json(j.text(), status);
    }
    static HttpResponse needLogin() { return HttpResponse::error(401, "login_required", "Please log in first."); }
    static HttpResponse ok() { return HttpResponse::json("{\"ok\":true}"); }

    // Reads a JSON body; returns false (and fills `err`) if it isn't JSON.
    static bool readJson(const HttpRequest& req, JsonValue& body, HttpResponse& err) {
        if (req.header("content-type").find("application/json") != 0) { err = HttpResponse::error(415, "json_only", "Send the data as JSON."); return false; }
        try { body = JsonParser::parse(req.body); }
        catch (const std::exception&) { err = HttpResponse::error(400, "bad_json", "The data could not be read."); return false; }
        if (!body.isObject()) { err = HttpResponse::error(400, "bad_json", "The data could not be read."); return false; }
        return true;
    }
    static std::string text(const JsonValue& v, const char* key) { const JsonValue* x = v.get(key); return x && x->isString() ? x->asString() : ""; }
    static long long number(const JsonValue& v, const char* key) { const JsonValue* x = v.get(key); return x && x->isNumber() ? (long long)x->asNumber() : 0; }

    // {"loggedIn":true,"id":"2510012","name":"Riya","role":"student"} (or {"loggedIn":false})
    static std::string userJson(const LfUser* u) {
        JsonWriter j;
        j.beginObject().key("loggedIn").boolean(u != nullptr);
        if (u) j.key("id").str(u->id).key("name").str(u->name).key("role").str(u->role);
        j.endObject();
        return j.text();
    }
    static std::string sessionCookie(const std::string& token, long long maxAge) {
        return "lf_session=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + std::to_string(maxAge);
    }

    AuthStore& auth_;
};

class SignupHandler : public AuthHandlerBase {
public:
    using AuthHandlerBase::AuthHandlerBase;
    bool allows(const std::string& m) const override { return m == "POST"; }
    HttpResponse handle(const HttpRequest& req) override {
        JsonValue b; HttpResponse err;
        if (!readJson(req, b, err)) return err;
        std::string roll = trim(text(b, "roll")), pw = text(b, "password"), name;
        std::string problem = AuthStore::nameProblem(text(b, "name"), name);
        if (!problem.empty()) return fieldError("name", problem);
        problem = auth_.rollProblem(roll);
        if (!problem.empty()) return fieldError("roll", problem);
        problem = AuthStore::passwordProblem(pw, roll);
        if (!problem.empty()) return fieldError("password", problem);
        if (text(b, "confirm") != pw) return fieldError("confirm", "The two passwords don't match.");
        switch (auth_.signup(roll, pw, name)) {
            case AuthStore::Result::Exists: return fieldError("roll", "An account with this roll number already exists. Log in instead.", 409);
            case AuthStore::Result::Ok: break;
            default: return HttpResponse::error(500, "save_failed", "Your account could not be saved. Please try again.");
        }
        HttpResponse r = HttpResponse::json(userJson(auth_.findUser(roll)), 201);
        r.extraHeaders.push_back({"Set-Cookie", sessionCookie(auth_.createSession(roll, now()), AuthStore::kSessionSeconds)});
        return r;
    }
};

class LoginHandler : public AuthHandlerBase {
public:
    using AuthHandlerBase::AuthHandlerBase;
    bool allows(const std::string& m) const override { return m == "POST"; }
    HttpResponse handle(const HttpRequest& req) override {
        JsonValue b; HttpResponse err;
        if (!readJson(req, b, err)) return err;
        std::string id = trim(text(b, "roll")), pw = text(b, "password");
        if (id.empty() || pw.empty()) return fieldError("roll", "Please enter your roll number and password.");
        if (id.size() > 40 || pw.size() > 64) return fieldError("roll", "Wrong roll number or password.", 401);
        int wait = 0;
        switch (auth_.login(id, pw, now(), wait)) {
            case AuthStore::Result::Locked:
                return fieldError("password", "Too many wrong passwords. Try again in " + std::to_string(wait) + " minutes.", 429);
            case AuthStore::Result::Ok: break;
            default: return fieldError("password", "Wrong roll number or password.", 401);   // never says which one
        }
        std::string token = auth_.createSession(id, now());
        HttpResponse r = HttpResponse::json(userJson(auth_.findUser(id)));
        r.extraHeaders.push_back({"Set-Cookie", sessionCookie(token, AuthStore::kSessionSeconds)});
        return r;
    }
};

class LogoutHandler : public AuthHandlerBase {
public:
    using AuthHandlerBase::AuthHandlerBase;
    bool allows(const std::string& m) const override { return m == "POST"; }
    HttpResponse handle(const HttpRequest& req) override {
        auth_.logout(req.cookie("lf_session"));
        HttpResponse r = ok();
        r.extraHeaders.push_back({"Set-Cookie", sessionCookie("", 0)});     // tells the browser to forget it
        return r;
    }
};

// POST /api/auth/name {"name": "..."} -- the logged-in student saves (or changes) their name.
class SetNameHandler : public AuthHandlerBase {
public:
    using AuthHandlerBase::AuthHandlerBase;
    bool allows(const std::string& m) const override { return m == "POST"; }
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        JsonValue b; HttpResponse err;
        if (!readJson(req, b, err)) return err;
        std::string name, problem = AuthStore::nameProblem(text(b, "name"), name);
        if (!problem.empty()) return fieldError("name", problem);
        std::string id = u->id;
        if (!auth_.setName(id, name)) return HttpResponse::error(500, "save_failed", "Your name could not be saved. Please try again.");
        return HttpResponse::json(userJson(auth_.findUser(id)));
    }
};

class MeHandler : public AuthHandlerBase {
public:
    using AuthHandlerBase::AuthHandlerBase;
    HttpResponse handle(const HttpRequest& req) override {
        return HttpResponse::json(userJson(user(req)));
    }
};

} // namespace unigo
