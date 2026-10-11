import subprocess,time,os,statistics,sys
ST="$PROJECTS/protoST"; BIN=ST+"/build_bench/protost"; B=os.path.dirname(__file__)
cases=[("parallel.st",B+"/parallel.st","21"),("pump_twin.st",ST+"/examples/pump_twin.st","3")]
for name,path,exp in cases:
  for w in (None,"1"):
    env=dict(os.environ); 
    if w: env["PROTOST_WORKERS"]=w
    ts=[]
    for i in range(4):
      t=time.perf_counter(); p=subprocess.run([BIN,path],env=env,capture_output=True,text=True,cwd=ST); ms=(time.perf_counter()-t)*1000
      assert p.returncode==0 and p.stdout.strip().splitlines()[-1]==exp,(name,p.stdout,p.stderr)
      if i: ts.append(ms)
    print(name,"workers="+(w or "default"),"median %.0f ms"%statistics.median(ts),[round(x) for x in ts])
