from pathlib import Path
import subprocess,json,sys
d=Path(__file__).resolve().parent;out=d/'diagnostics';out.mkdir(exist_ok=True)
expected={'board_seating':False,'closed':False,'rigid_insertion':False,'components':False,'catch_left':True,'catch_right':True,'release':False,'release_board':False,'allowed_play':False,'locating_x':True,'locating_y':True,'din_seated':False,'din_slide_path':False,'din_end_stop':True,'din_detent':True,'din_rail_seated':False,'din_hook_fixed':True,'din_hook_latch':True,'din_latch_path':False,'din_latch_free':False,'din_tilt_rotate':False,'din_tilt_unhook':False}
results_path=d/'geometry-results.json'
results=json.loads(results_path.read_text()) if results_path.exists() else {}
selected=sys.argv[1:] or list(expected)
assert all(name in expected for name in selected), 'Unknown check'
for name in selected:
 positive=expected[name]
 f=out/(name+'.stl'); f.unlink(missing_ok=True)
 p=subprocess.run(['openscad','-o',str(f),'-D','part="none"','-D',f'check="{name}"',str(d/'verify_geometry.scad')],capture_output=True,text=True)
 log=p.stdout+p.stderr;(out/(name+'.log')).write_text(log)
 has=f.exists() and 'vertex' in f.read_text(); empty='Current top level object is empty.' in log
 ok=('ERROR' not in log and 'WARNING' not in log and ((positive and p.returncode==0 and has) or (not positive and empty and not has)))
 results[name]={'passed':ok,'expected':'solid' if positive else 'empty','exit_code':p.returncode}
 print(name, 'PASS' if ok else 'FAIL',flush=True)
 if not ok: print(log[-1600:],flush=True)
 f.unlink(missing_ok=True)
(d/'geometry-results.json').write_text(json.dumps(results,indent=2)+'\n')
sys.exit(0 if all(x['passed'] for x in results.values()) else 1)
