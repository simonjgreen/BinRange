#pragma once
#include "Arduino.h"
#include <functional>
#include <map>
#include <utility>
#include <vector>

enum HTTPMethod { HTTP_GET, HTTP_POST };
enum HTTPAuthMethod { BASIC_AUTH, DIGEST_AUTH };

// Request data and SDK response/authentication boundary; handlers are the real
// lambdas registered by admin_begin(). Digest parsing is tested in core tests.
class WebServer {
 public:
  std::map<String, String> headers;
  std::map<String, String> response_headers;
  std::map<std::pair<String, HTTPMethod>, std::function<void()>> routes;
  std::vector<std::pair<String, String>> form;
  String request_uri = "/api/admin";
  String auth_password = "fixture password";
  String response;
  int status = 0;
  unsigned auth_calls = 0;
  unsigned challenges = 0;
  String header(String name) { return headers[name]; }
  String uri() { return request_uri; }
  int args() { return static_cast<int>(form.size()); }
  String argName(int i) { return form.at(i).first; }
  String arg(int i) { return form.at(i).second; }
  bool authenticate(const char *name, const char *value) {
    ++auth_calls;
    return String(name) == "admin" && String(value) == auth_password;
  }
  void sendHeader(String name, String value) { response_headers[name] = value; }
  void send(int code, const char *, const String &body) { status = code; response = body; }
  void requestAuthentication(HTTPAuthMethod mode, const char *realm) {
    if (mode != DIGEST_AUTH || String(realm) != "BinRange Admin") std::abort();
    ++challenges;
    status = 401;
  }
  void on(const char *uri, HTTPMethod method, std::function<void()> handler) {
    routes[{uri, method}] = handler;
  }
  void dispatch(const char *uri, HTTPMethod method) {
    request_uri = uri;
    routes.at({uri, method})();
  }
};
