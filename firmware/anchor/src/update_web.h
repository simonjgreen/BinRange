#pragma once
#include <WebServer.h>

// Pinned WebServer 2.0.14 exposes multipart fields in _postArgs during file
// callbacks, moving them to _currentArgs only after parsing the complete body.
class UpdateWebServer : public WebServer {
 public:
    using WebServer::WebServer;
    bool upload_manifest(String &out) const;
    void wipe_request_arguments();
};
void update_web_begin(UpdateWebServer &server);
// Called around the actual synchronous HTTP request, including parser failures.
void update_web_request_begin();
void update_web_request_end();
