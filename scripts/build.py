#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path

def run_command(command, cwd):
    print(f"Running: {' '.join(command)}")
    result = subprocess.run(command, cwd=cwd)
    if result.returncode != 0:
        print(f"Error: Command {' '.join(command)} failed.")
        sys.exit(result.returncode)

def print_argument_info():
    info_text = """
Build Project Script Argument Information:
-------------------------------------------
--help, -h       : Show this help message and exit.
--release        : Build in Release mode (default). You can be explicit.
--debug          : Build in Debug mode.
--web            : Build for web using emcmake.
--jobs, -j       : Number of parallel build jobs (default: 8).
--info           : Print information about build arguments and exit.

Available Configurations:
1. Native Release: 
   cmake -DCMAKE_BUILD_TYPE=Release -S . -B build-release
   cmake --build build-release -j8

2. Native Debug:
   cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build-debug
   cmake --build build-debug -j8

3. Web Release:
   emcmake cmake -DCMAKE_BUILD_TYPE=Release -B build-web-release
   cmake --build build-web-release -j8

4. Web Debug:
   emcmake cmake -DCMAKE_BUILD_TYPE=Debug -B build-web-debug
   cmake --build build-web-debug -j8
-------------------------------------------
"""
    print(info_text)

def print_configuration_details(build_type, is_web, build_dir, jobs):
    target = "Web" if is_web else "Native"
    details = f"""
-------------------------------------------
Build Configuration Details:
  Build Type   : {build_type}
  Build Target : {target}
  Build Dir    : {build_dir}
  Jobs         : {jobs}
-------------------------------------------
"""
    print(details)

def main():
    parser = argparse.ArgumentParser(
        description="Build the C++ project with different configurations."
    )
    
    # Mutually exclusive group for build type options
    build_type_group = parser.add_mutually_exclusive_group()
    build_type_group.add_argument("--debug", action="store_const", dest="build_type", const="Debug",
                                  help="Build in Debug mode")
    build_type_group.add_argument("--release", action="store_const", dest="build_type", const="Release",
                                  help="Build in Release mode (default)")
    parser.set_defaults(build_type="Release")
    
    parser.add_argument("--web", action="store_true",
                        help="Build for web (using emcmake)")
    parser.add_argument("--jobs", "-j", type=int, default=8,
                        help="Number of parallel build jobs (default: 8)")
    parser.add_argument("--info", action="store_true",
                        help="Print information about build arguments and exit")
    
    args = parser.parse_args()

    if args.info:
        print_argument_info()
        return

    # Set base directory to one level up from the script directory
    base_dir = Path(__file__).resolve().parent.parent
    print(f"Project base directory: {base_dir}")

    if args.web:
        # Web build configuration
        if args.build_type == "Debug":
            build_dir = "build-web-debug"
        else:
            build_dir = "build-web-release"
        cmake_command = ["emcmake", "cmake", f"-DCMAKE_BUILD_TYPE={args.build_type}", "-B", build_dir]
        build_command = ["cmake", "--build", build_dir, f"-j{args.jobs}"]
    else:
        # Native build configuration
        if args.build_type == "Debug":
            build_dir = "build-debug"
        else:
            build_dir = "build-release"
        cmake_command = ["cmake", f"-DCMAKE_BUILD_TYPE={args.build_type}", "-S", ".", "-B", build_dir]
        build_command = ["cmake", "--build", build_dir, f"-j{args.jobs}"]

    print_configuration_details(args.build_type, args.web, build_dir, args.jobs)
    run_command(cmake_command, cwd=base_dir)

    print("Building project:")
    run_command(build_command, cwd=base_dir)

if __name__ == "__main__":
    main()
