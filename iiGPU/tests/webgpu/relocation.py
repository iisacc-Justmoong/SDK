"""Install, move, compile and execute the package without loader-path overrides."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import resource_workspace


def main(argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    compile_only = "--compile-only" in arguments
    if compile_only:
        arguments.remove("--compile-only")
    if not 2 <= len(arguments) <= 5:
        raise ValueError("Expected source, build, optional config and Vulkan paths")
    source=Path(arguments[0]).resolve()
    build_argument=Path(arguments[1])
    if build_argument.is_symlink():
        raise ValueError("WebGPU build directory cannot be a symlink")
    build=build_argument.resolve()
    config=arguments[2] if len(arguments)>2 and arguments[2] else "Release"
    env=os.environ.copy()
    for key in tuple(env):
        if key.startswith(("DYLD_","LD_LIBRARY_PATH")):
            env.pop(key)
    def run(args):
        completed=subprocess.run(args,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if completed.returncode:
            raise RuntimeError(completed.stdout)
    with resource_workspace.temporary(source, "webgpu-relocation-") as temporary, \
         resource_workspace.compiled(build) as consumer_build:
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
        configure=["cmake","-S",str(source/"tests/webgpu/consumer"),"-B",str(consumer_build),
                   "-DCMAKE_BUILD_TYPE="+config,"-DCMAKE_PREFIX_PATH="+str(moved)]
        # Vulkan belongs to the base iiGPU target and may be installed in
        # a nonstandard SDK path; preserve the producer's discovered SDK.
        for name, value in zip(("Vulkan_INCLUDE_DIR", "Vulkan_LIBRARY"), arguments[3:5]):
            if value and not value.endswith("-NOTFOUND"):
                configure.append("-D"+name+"="+value)
        run(configure)
        run(["cmake","--build",str(consumer_build),"--config",config,"--parallel","2"])
        if not compile_only:
            run(["ctest","--test-dir",str(consumer_build),"-C",config,"--output-on-failure"])
    print("Relocated installed consumer built" +
          (" without GPU execution" if compile_only else " and executed on the hardware GPU"))



if __name__=="__main__":
    main()
