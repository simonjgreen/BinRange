"""Run the real legacy web handlers against host-side hardware boundaries."""
import json
import pathlib
import subprocess
import tempfile
import unittest

from test_anchor_web_build import extract_function

ROOT = pathlib.Path(__file__).resolve().parents[1]

# Only Arduino strings, HTTP request/response and hardware persistence are faked.
# Handler bodies come directly from webui.cpp; auth policy has its own native suite.
HARNESS = r'''
#include <string>
#include <map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <type_traits>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string &s):std::string(s){}
  template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
  String(T n):std::string(std::to_string(n)){}
  long toInt() const { return std::strtol(c_str(), nullptr, 10); }
  void trim() { auto a=find_first_not_of(" \t\r\n"), b=find_last_not_of(" \t\r\n");
    *this=a==npos ? "" : substr(a,b-a+1); }
  void replace(const char *a, const char *b) { size_t p=0;
    while((p=find(a,p))!=npos) { std::string::replace(p,strlen(a),b); p+=strlen(b); } }
};
struct Server {
 std::map<std::string,String> form; int status=0; String response;
 bool hasArg(const char *k){return form.count(k);}
 String arg(const char *k){return form[k];}
 void send(int code,const char *,String body){status=code; response=body;}
 void sendHeader(const char *,const char *){}
} server;
bool allowed=false; unsigned mutations=0; bool mutation_requested=false;
bool admin_authorize(Server &s,bool mutation){mutation_requested=mutation;
 if(!allowed)s.send(403,"text/plain","Forbidden"); return allowed;}
size_t strlcpy(char *d,const char *s,size_t n){size_t len=strlen(s);
 if(n){strncpy(d,s,n-1);d[n-1]=0;}return len;}
enum Phy {PHY_SHORT,PHY_LONG,PHY_MAX,PHY_COUNT};
unsigned ranging_antdly(){return 100;}
Phy ranging_phy(){return PHY_LONG;}
unsigned ranging_xtrim(){return 0;}
void ranging_set_antdly(uint16_t){++mutations;}
void ranging_set_phy(Phy){++mutations;}
void ranging_set_xtrim(uint8_t){++mutations;}
String hostname="anchor";
void webui_set_hostname(String h){hostname=h;++mutations;}
void stats_reset(){++mutations;}
void delay(unsigned){}
struct {void restart(){++mutations;}} ESP;
struct BrokerCfg {char host[64];uint16_t port;char user[32],pass[64],client_id[32];};
BrokerCfg stored={"broker",1883,"user","secret","anchor"};
void mqttcfg_load(BrokerCfg *c){*c=stored;}
void mqttcfg_save(const BrokerCfg *c){stored=*c;++mutations;}
void mqtt_reconnect_now(){++mutations;}
bool mqttcfg_has_password(){return true;}
struct MqttActivity {uint32_t published=0,failed=0,reconnects=0;
 int last_rc=0;uint32_t last_pub_ms=0;bool connected=true;};
void mqtt_activity(MqttActivity *a){*a=MqttActivity{};}
const char *mqtt_broker_desc(){return "broker\\name";}
const char *mqtt_state_text(int){return "connected";}
uint32_t millis(){return 100;}
size_t mqtt_log_size(){return 1;}
const char *mqtt_log_at(size_t,uint32_t *age){*age=0;return "topic\tline\n\x01\"\\";}
'''


class AnchorWebRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = pathlib.Path(cls.temp.name) / 'web-test'
        source = (ROOT / 'firmware/anchor/src/webui.cpp').read_text()
        functions = []
        if 'static String json_string(' in source:
            functions.append(extract_function(source, 'static String json_string('))
        for name in ('config', 'mqtt_post', 'reset', 'reboot', 'mqtt_get'):
            functions.append(extract_function(source, f'static void handle_{name}()'))
        main = r'''
int main(int argc,char **argv){
 if(argc!=3)return 2;
 allowed=std::string(argv[2])=="allow";
 std::string action=argv[1];
 server.form={{"host","replacement"},{"antdly","200"}};
 if(action=="config")handle_config();
 if(action=="mqtt")handle_mqtt_post();
 if(action=="reset")handle_reset();
 if(action=="reboot")handle_reboot();
 if(action=="json"){
  strlcpy(stored.user,"a\"b\\c\td",sizeof(stored.user));
  handle_mqtt_get();std::cout<<server.response;return 0;
 }
 std::cout<<server.status<<" "<<mutations<<" "<<mutation_requested;
}
'''
        cpp = pathlib.Path(cls.temp.name) / 'web-test.cpp'
        cpp.write_text(HARNESS + '\n'.join(functions) + main)
        subprocess.run(['c++', '-std=c++17', str(cpp), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_denied_mutations_cannot_change_settings_or_restart(self):
        for action in ('config', 'mqtt', 'reset', 'reboot'):
            with self.subTest(action=action):
                result = subprocess.check_output([self.binary, action, 'deny'], text=True)
                self.assertEqual(result, '403 0 1')

    def test_authorized_mutations_remain_usable(self):
        for action in ('config', 'mqtt', 'reset', 'reboot'):
            with self.subTest(action=action):
                result = subprocess.check_output([self.binary, action, 'allow'], text=True)
                status, mutations, gate = map(int, result.split())
                self.assertEqual(status, 200)
                self.assertGreater(mutations, 0)
                self.assertEqual(gate, 1)

    def test_mqtt_json_preserves_quoted_and_control_characters(self):
        raw = subprocess.check_output([self.binary, 'json', 'allow'], text=True)
        payload = json.loads(raw)
        self.assertEqual(payload['user'], 'a"b\\c\td')
        self.assertEqual(payload['broker'], 'broker\\name')
        self.assertEqual(payload['log'][0]['t'], 'topic\tline\n\x01"\\')
        self.assertNotIn('secret', raw)


if __name__ == '__main__':
    unittest.main()
