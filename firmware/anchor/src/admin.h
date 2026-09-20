#pragma once

class WebServer;

// Installs the private admin boundary after TCP/IP is available. No credential
// accessor is exposed: consumers can only apply it to the paired OTA service.
void admin_begin(WebServer &server);
bool admin_configured();
bool admin_authorize(WebServer &server, bool mutation);
void admin_apply_ota_credentials();
