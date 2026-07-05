import sys
import subprocess
import os

def main():
    if len(sys.argv) < 2:
        return
    
    compiler_script = sys.argv[1] # C:/Users/.../prospero-clang
    args = sys.argv[2:]
    
    is_cxx = "clang++" in compiler_script
    
    sdk = "C:/Users/Blurf/ps5dev/toolchains/ps5-payload-sdk"
    clang_exe = r"C:\Users\Blurf\scoop\apps\llvm\current\bin\clang.exe"
    if is_cxx:
        clang_exe = r"C:\Users\Blurf\scoop\apps\llvm\current\bin\clang++.exe"
    
    if not os.path.exists(clang_exe):
        clang_exe = "clang++" if is_cxx else "clang"
        
    cmd = [clang_exe]
    
    cmd += ["-target", "x86_64-sie-ps5"]
    cmd += ["-fvisibility-nodllstorageclass=default"]
    cmd += ["-isysroot", sdk]
    cmd += ["--sysroot", sdk]

    if is_cxx:
        cmd += ["-stdlib++-isystem", f"{sdk}/target/include/c++/v1"]
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
