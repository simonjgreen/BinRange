"""Exercise the shipped anchor page's admin actions without a device."""
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class AnchorWebUiTests(unittest.TestCase):
    def test_settings_actions_authenticate_and_never_replay_failed_posts(self):
        harness = r'''
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const page=fs.readFileSync(process.argv[1],'utf8');
const script=page.split('<script>')[1].split('</script>')[0];
const ids=[...page.matchAll(/\bid=["']?([\w-]+)/g)].map(m=>m[1]);
async function check(action, failure){
 const nodes=Object.fromEntries(ids.map(id=>[id,{value:'',textContent:'',disabled:false,
  addEventListener(){},getContext(){return {};}}]));
 const context={document:{getElementById:id=>nodes[id]},URLSearchParams,AbortController,
  setTimeout,clearTimeout,setInterval(){},confirm:()=>true,
  fetch:async()=>{throw Error('initial polling offline');}};
 vm.createContext(context);vm.runInContext(script,context);
 await new Promise(resolve=>setImmediate(resolve));
 nodes.m_pass.value='private password';nodes.m_host.value='broker';
 const requests=[];
 context.fetch=async(path,options={})=>{
  requests.push({path,options});
  if(path==='/api/admin')return {ok:failure!=='auth',status:failure==='auth'?401:200,
   json:async()=>({csrf:'boot-token'})};
  if(options.method==='POST'){
   if(failure==='network')throw Error('disconnected');
   return {ok:failure!=='post',status:failure==='post'?403:200,text:async()=>''};
  }
  throw Error('polling offline');
 };
 await vm.runInContext(action+'()',context);
 const admin=requests.filter(r=>r.path==='/api/admin');
 const posts=requests.filter(r=>r.options.method==='POST');
 assert.equal(admin.length,1,action+' must authenticate first');
 assert.equal(posts.length,failure==='auth'?0:1,action+' must not replay writes');
 if(posts.length){
  assert.equal(posts[0].options.credentials,'same-origin');
  assert.equal(posts[0].options.headers['X-BinRange-CSRF'],'boot-token');
  assert(!posts[0].path.includes('?'),'settings/secrets must be in POST body');
  if(action==='saveMqtt')assert.equal(posts[0].options.body.get('pass'),'private password');
 }
 if(failure){
  assert(nodes.actionStatus.textContent.length>0,'show the failed action');
  if(action==='saveMqtt')assert.equal(nodes.m_pass.value,'private password');
 }else if(action==='saveMqtt')assert.equal(nodes.m_pass.value,'');
}
(async()=>{for(const action of ['save','reset','saveMqtt','reboot'])
 for(const failure of ['', 'auth','post','network'])await check(action,failure);
})().catch(error=>{console.error(error);process.exitCode=1;});
'''
        run = subprocess.run(['node', '-e', harness,
                              str(ROOT / 'firmware/anchor/src/webui_page.h')],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
