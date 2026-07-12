import sys
import subprocess
import os
import shutil

def main():
    if len(sys.argv) < 2:
        return
    
    compiler_script = sys.argv[1]
    args = sys.argv[2:]
    
    compiler_name = os.path.basename(compiler_script).lower()
    is_cxx = "++" in compiler_name or "c++" in compiler_name
    
    sdk = os.environ.get("PS5_PAYLOAD_SDK")
    if not sdk:
        print("compile_wrapper.py: PS5_PAYLOAD_SDK is not set", file=sys.stderr)
        sys.exit(2)

    llvm_bindir = os.environ.get("LLVM_BINDIR")
    clang_name = "clang++.exe" if is_cxx and os.name == "nt" else "clang++" if is_cxx else "clang.exe" if os.name == "nt" else "clang"
    clang_exe = os.path.join(llvm_bindir, clang_name) if llvm_bindir else ""
    
    if not clang_exe or not os.path.exists(clang_exe):
        found = shutil.which("clang++" if is_cxx else "clang")
        if not found:
            print("compile_wrapper.py: clang not found; set LLVM_BINDIR or PATH", file=sys.stderr)
            sys.exit(2)
        clang_exe = found
        
    cmd = [clang_exe]
    
    cmd += ["-target", "x86_64-sie-ps5"]
    cmd += ["-fvisibility-nodllstorageclass=default"]
    cmd += ["-isysroot", sdk]
    cmd += ["--sysroot", sdk]

    if is_cxx:
        cmd += ["-stdlib++-isystem", os.path.join(sdk, "target", "include", "c++", "v1")]
        cmd += ["-frtti", "-fexceptions"]
    cmd += ["-fno-stack-protector", "-fno-plt", "-femulated-tls"]
    
    new_args = []
    for arg in args:
        if arg.startswith("-DVERSION_TAG=") or arg.startswith("-DBUILD_VERSION=") or arg.startswith("-DBFPILOT_PAYLOAD_NAME=") or arg.startswith("-DBFPILOT_BUILD_MODE="):
            val = arg.split('=', 1)[1]
            if val.startswith('\\"') and val.endswith('\\"'):
                val = '"' + val[2:-2] + '"'
            elif val.startswith('"') and val.endswith('"'):
                val = '"' + val[1:-1] + '"'
            else:
                val = '"' + val + '"'
            
            key = arg.split('=', 1)[0]
            new_args.append(f"{key}={val}")
        else:
            new_args.append(arg)
            
    cmd.extend(new_args)
    
    res = subprocess.run(cmd)
    sys.exit(res.returncode)

if __name__ == "__main__":
    main()
