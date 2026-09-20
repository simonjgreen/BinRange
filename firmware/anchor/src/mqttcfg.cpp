#include "mqttcfg.h"
#include <Preferences.h>
#include "secrets.h"
#include "config.h"

static Preferences p;

void mqttcfg_load(BrokerCfg *c) {
    p.begin("mqtt", true);
    strlcpy(c->host, p.getString("host", MQTT_HOST).c_str(), sizeof(c->host));
    c->port = p.getUShort("port", MQTT_PORT);
    strlcpy(c->user, p.getString("user", MQTT_USER).c_str(), sizeof(c->user));
    strlcpy(c->pass, p.getString("pass", MQTT_PASS).c_str(), sizeof(c->pass));
    strlcpy(c->client_id,
            p.getString("cid", "binrange-anchor-" ANCHOR_ID).c_str(),
            sizeof(c->client_id));
    p.end();
}

void mqttcfg_save(const BrokerCfg *c) {
    p.begin("mqtt", false);
    p.putString("host", c->host);
    p.putUShort("port", c->port);
    p.putString("user", c->user);
    p.putString("pass", c->pass);   // caller keeps the old value if blank
    p.putString("cid", c->client_id);
    p.end();
}

bool mqttcfg_has_password() {
    BrokerCfg c;
    mqttcfg_load(&c);
    return c.pass[0] != '\0';
}
