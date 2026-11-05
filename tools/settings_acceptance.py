"""Real SDL menu and fresh-process restart checks; original assets stay local."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from PIL import Image


DEFAULTS = dict(presentation="original", fixes="recommended", scale="3", fullscreen="no",
    speaker="realsound", timing="dosbox", ips="60000000", crt="no", subtitles="no", sound="ask",
    roland="off", opl="dbopl", engine="recomp", colourblind="off")
DEFAULTS.update({"crt-mask":"no", "crt-glow":"no", "music-source":"default", "sound-source":"default",
    "adlib-effects":"yes", "sim-hz":"15", "no-aspect":"no", "present-scale":"auto",
    "present-line-width":"auto", "present-age":"interp", "draw-distance":"original", "dot-distance":"original", "pick-order":"exact", "pad-deadzone":"0", "pad-fire1":"0",
    "pad-fire2":"1", "pad-invert-x":"no", "pad-invert-y":"no"})


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def ini_values(text):
    return {line.split("=",1)[0].strip(): line.split("=",1)[1].strip()
            for line in text.splitlines() if "=" in line and not line.lstrip().startswith((";","#"))}


def check_restart(log):
    require(re.search(r"\[settings-restart\] pid [1-9]\d*",log), "a fresh process was not started")
    require("[settings-start] child=1 sound=3 presentation=enhanced" in log,
            "saved presentation or active sound was lost across restart")
    require("[settings-autoanswer] child=1 N 3" in log, "fresh SETUP did not receive its normal answers")
    require("[settings-module] child=1 MPS_LOGO.EXE" in log, "fresh process did not progress past SETUP")
    require("[settings-exit] child=1 rc=0" in log, "fresh process did not finish successfully")
    require(log.index("[settings-start] child=1")<log.index("[settings-autoanswer] child=1")<
            log.index("[settings-module] child=1 MPS_LOGO.EXE")<log.rindex("[settings-exit] child=1"),
            "fresh-process evidence is out of order")


def wait_child(pid, timeout):
    """Wait on the process handle belonging to our traced restart, not a name."""
    if os.name!="nt":
        return
    import ctypes
    from ctypes import wintypes
    kernel=ctypes.WinDLL("kernel32",use_last_error=True)
    kernel.OpenProcess.argtypes=(wintypes.DWORD,wintypes.BOOL,wintypes.DWORD)
    kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes=(wintypes.HANDLE,wintypes.DWORD)
    kernel.CloseHandle.argtypes=(wintypes.HANDLE,)
    kernel.TerminateProcess.argtypes=(wintypes.HANDLE,wintypes.UINT)
    handle=kernel.OpenProcess(0x101001,False,pid)
    if not handle:  # It can already have exited; the ordered exit record is still required.
        return
    try:
        status=kernel.WaitForSingleObject(handle,int(timeout*1000))
        if status==258:
            kernel.TerminateProcess(handle,1)
            kernel.WaitForSingleObject(handle,5000)
            raise ValueError("owned settings restart timed out and was terminated")
        require(status==0,"could not observe restarted process completion")
    finally:
        kernel.CloseHandle(handle)


def run(app, data, session, out, timeout=45):
    """Runs only owned processes, with private configs/saves and bounded clocks."""
    app,data,session,out=map(lambda p:Path(p).resolve(),(app,data,session,out))
    require(not out.exists() or not any(out.iterdir()), "settings output must be new or empty")
    out.mkdir(parents=True,exist_ok=True)
    require((session/"save-start").is_dir(), "settings check needs private starting saves")
    base="presentation = original\nsound = ask\nroland = off\n"
    changed=base+f"data = {data}\nmt32-roms = kept-rom-folder\nips = 9000000\ncrt = yes\nfixes = custom\nfix = D4\nremap = Q:W\npad-keys = 2:G\npad-invert-x = yes\n"
    # Root rows: Video settings (0), six main settings (1-6), Audio (7), Advanced (8), Save (9), ..., Reset (14), Close (15).
    down=lambda n:["Down"]*n
    cases=[
        ("audio",base,["Open"]+down(7)+["Return","Escape","Escape"],"audio",None,[]),
        ("advanced",base,["Open"]+down(8)+["Return","Escape","Escape"],"advanced",None,[]),
        ("keys-remap",base,["OpenKeys","Return","Q","Escape"],"keys-tab",None,[]),
        ("keys-restore",base+"remap = Q:=,=:Q\n",["OpenKeys","Backspace","Escape"],"keys-tab",None,[]),
        ("reset",changed,["Open"]+down(14)+["Return"]+["Up"]*5+["Return"],"reset",None,["--ips","9000000","--crt","--engine","interp","--fix","none","--remap","A:B","--pad-keys","3:B"]),
        ("cancel-reset",changed,["Open"]+down(14)+["Return","Escape"],"reset",None,[]),
        ("fresh restart",base,["Open","Return","Right","Escape"]+down(9)+["Return"],None,None,["--presentation","original"]),
    ]
    report=dict(app_sha256=digest(app),checker_sha256=digest(__file__),cases=[])
    started=time.monotonic()
    for name,text,script,shot_state,shot_key,extra in cases:
        folder=out/name;folder.mkdir();shutil.copytree(session/"save-start",folder/"save")
        config=folder/"settings.ini";config.write_text(text)
        env={k:v for k,v in os.environ.items() if not k.startswith("F117_")}
        env.update(SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy",SDL_RENDER_DRIVER="software",F117_WINDOW="1280x960",
                   F117_SETTINGS_SCRIPT=",".join(script),F117_SETTINGS_TRACE="1",F117_SETTINGS_AUTOTEST="1")
        if name!="fresh restart":env["F117_RESTART_DRYRUN"]="1"
        if shot_state=="keys-tab":env.update(F117_MENU_SHOT=str(folder/"menu.ppm"),F117_REF_AUTOTEST="1")
        elif shot_state:env.update(F117_MENU_SHOT=str(folder/"menu.ppm"),F117_MENU_SHOT_STATE=shot_state)
        if shot_key:env["F117_MENU_SHOT_KEY"]=shot_key
        # The final backslash exercises actual Windows restart argument quoting.
        save=str(folder/"save")+("\\" if os.name=="nt" else "/")
        command=[str(app),"--config",str(config),"--data",str(data),"--save",save,"--no-record",
                 "--sound","roland","--roland","off","--exit-after","50000000",*extra]
        with (folder/"process.log").open("w") as stream:
            process=subprocess.run(command,env=env,stdout=stream,stderr=stream,timeout=timeout)
        require(process.returncode==0,f"{name}: app failed; see process.log")
        log=(folder/"process.log").read_text()
        if name=="fresh restart":
            match=re.search(r"\[settings-restart\] pid ([1-9]\d*)",log)
            require(match,"fresh restart supplied no process id")
            wait_child(int(match[1]),timeout)
            deadline=time.monotonic()+timeout
            while "[settings-exit] child=1" not in log and time.monotonic()<deadline:
                time.sleep(.1);log=(folder/"process.log").read_text()
            check_restart(log)
        counts=re.findall(r"\[settings-bindings\] supported=(\d+) listed=(\d+)",log)
        if shot_state!="keys-tab":require(counts and all(int(a)==int(b) and 80<=int(a)<=128 for a,b in counts),f"{name}: incomplete key list")
        values=ini_values(config.read_text())
        if name=="reset":
            require(all(values.get(k)==v for k,v in DEFAULTS.items()),"reset did not persist every default")
            require(not any(k in values for k in ("fix","remap","pad-keys")),"reset left custom bindings or fixes")
            require(values.get("data")==str(data) and values.get("mt32-roms")=="kept-rom-folder","reset removed resource folders")
            require("--fix none" not in log,"reset restart retained a conflicting fixes override")
            defaults=dict(re.findall(r"\[settings-default-fix\] (\w+)=(\d)",log))
            require(defaults.get("QUIZ")=="0" and len(defaults)>30 and
                    all(value=="1" for key,value in defaults.items() if key!="QUIZ"),"reset fix-picker defaults disagree with normal-play policy")
        elif name=="cancel-reset":require(config.read_text()==text,"cancelled reset changed the file")
        elif name=="keys-remap":require(values.get("remap")=="Q:=,=:Q","Keys tab did not save the swapped binding")
        elif name=="keys-restore":require("remap" not in values,"Keys tab did not restore the default binding")
        elif name=="fresh restart":require(values.get("sound")=="roland" and values.get("presentation")=="enhanced","restart choices were not persisted")
        if shot_state=="keys-tab":
            with Image.open(folder/"menu.ppm") as im:
                picture=im.convert("RGB");picture.save(folder/"menu.png")
                blue=sum(p==(96,176,255) for p in picture.getdata())
                require((blue>0)==(name=="keys-remap"),"changed key row is not blue (or a default one is)")
        elif shot_state:
            with Image.open(folder/"menu.ppm") as im:
                require(im.size==(640,400),"unexpected menu picture size")
                picture=im.convert("RGB");picture.save(folder/"menu.png")
                # Both tabs and the settings panel have known, independently reviewed bounds.
                for y in range(400):
                    for x in range(640):
                        if not (120<=x<520 and 36<=y<364) and not (40<=x<600 and 6<=y<30):
                            require(picture.getpixel((x,y))==(0,0,0),f"{name}: menu text spills outside its panel")
        report["cases"].append(dict(name=name,status="pass",supported_keys=int(counts[-1][0]) if counts else 0))
        (out/"report.json").write_text(json.dumps(report,indent=2))
    report.update(status="pass",seconds=time.monotonic()-started)
    (out/"report.json").write_text(json.dumps(report,indent=2))
    return report


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ("app","data","session","out"):parser.add_argument("--"+key,type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(run(args.app,args.data,args.session,args.out),indent=2))
