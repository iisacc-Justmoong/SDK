"""Install, move, compile and execute the package without loader-path overrides."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    source=Path(sys.argv[1]).resolve()
    build=Path(sys.argv[2]).resolve()
    config=sys.argv[3] if len(sys.argv)>3 and sys.argv[3] else "Release"
    env=os.environ.copy()
    for key in tuple(env):
        if key.startswith(("DYLD_","LD_LIBRARY_PATH")):
            env.pop(key)
    commands=[]
    def run(args):
        completed=subprocess.run(args,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        commands.append({"command":list(map(str,args)),"exit_code":completed.returncode,"output":completed.stdout[-18000:]})
        if completed.returncode:
            raise RuntimeError(completed.stdout)
    try:
        with tempfile.TemporaryDirectory(prefix="relocation-",dir=build) as temporary:
            root=Path(temporary)
            original=root/"original"
            moved=root/"moved"
            run(["cmake","--install",str(build),"--config",config,"--prefix",str(original)])
            shutil.move(str(original),str(moved))
            # The exported package itself must not retain the original install,
            # provider source, or iiWebGPU build directories.
            for path in list((moved/"lib/cmake/iiWebGPU").glob("*.cmake")) + list((moved/"lib/cmake/iiGPU").glob("*.cmake")):
                text=path.read_text()
                for forbidden in (str(original), str(source), str(build)):
                    if forbidden in text:
                        raise RuntimeError("Non-relocatable export: "+str(path))
            configure=["cmake","-S",str(source/"tests/webgpu/consumer"),"-B",str(root/"build"),
                       "-DCMAKE_BUILD_TYPE="+config,"-DCMAKE_PREFIX_PATH="+str(moved)]
            # Vulkan belongs to the base iiGPU target and may be installed in
            # a nonstandard SDK path; preserve the producer's discovered SDK.
            for name, value in zip(("Vulkan_INCLUDE_DIR", "Vulkan_LIBRARY"), sys.argv[4:6]):
                if value and not value.endswith("-NOTFOUND"):
                    configure.append("-D"+name+"="+value)
            run(configure)
            run(["cmake","--build",str(root/"build"),"--config",config,"--parallel","2"])
            run(["ctest","--test-dir",str(root/"build"),"-C",config,"--output-on-failure"])
        print("Relocated installed consumer built and executed on the hardware GPU")
    finally:
        (build/"relocation-report.json").write_text(json.dumps({"commands":commands},indent=2)+"\n")


if __name__=="__main__":
    main()
