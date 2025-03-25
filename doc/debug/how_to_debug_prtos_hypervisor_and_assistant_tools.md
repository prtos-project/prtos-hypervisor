# How to debug host assistant tools and prtos hypervisor for X86 target machine
## Step1:Download some necessary extensions
* NET Install Tool for Extension Authors
* CMake Language Support
* clangd
* CodeLLDB

## Step2:Add “launch.json” and “tasks.json” in “.vscode” folder

```
prtos-hypervisor home directory
|-- .vscode
   |-- launch.json
   |-- tasks.json
|-- vscode-build
|-- core
|-- user
|......
```

Here is the content of `launch.json`:
```
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "prtospack check",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/user/tools/prtospack/prtospack",
      "args": [
        "check",
        "${workspaceFolder}/user/bail/examples/helloworld/prtos_cf.pef.prtos_conf",
        "-h",
        "${workspaceFolder}/core/prtos_core.pef:${workspaceFolder}/user/bail/examples/helloworld/prtos_cf.pef.prtos_conf"        
      ],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}/user/bail/examples/helloworld",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        },
        { "text": "set output-radix 16" }
      ],
      "preLaunchTask": "prtos-build"
    },
    {
      "name": "prtoseformat build",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/user/tools/pef/prtoseformat",
      "args": [
        "build",
        "${workspaceFolder}/core/prtos_core",
        "-o",
        "${workspaceFolder}/core/prtos_core.pef"        
      ],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}/core",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ],
      "preLaunchTask": "prtos-build"
    },
    {
      "name": "prtos core debug",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/core/prtos_core",
      "cwd": "${workspaceFolder}",
      "MIMode": "gdb",
      "miDebuggerPath":"/usr/bin/gdb",
      "miDebuggerServerAddress": "localhost:1234"
    }
  ]
}

```
Here is the content of `tasks.json`:

```
{
    "tasks": [
        {
            "label": "prtos-build",
            "command": "make",
            "args": ["-B"
            ],
            "options": {
                "cwd": "${workspaceFolder}/"
            },
            "dependsOn": [
                "make"
            ]
        }
    ],
    "version": "2.0.0"
}
```
When debugging prtos-hypervisor code, pls run the hypervisor on the qemu-system-i386 hypervisor.

Here is the demo picture to debug prtos core on qemu x86 platform:
![prtos core debug on qemu x86 platform](https://github.com/prtos-project/prtos-hypervisor/blob/main/doc/figures/prtos_core_debug_on_x86.jpg)


# How to debug host assistant tools and prtos hypervisor for ARMv8 target machine
## Step1:Download some necessary extensions
* NET Install Tool for Extension Authors
* CMake Language Support
* clangd
* CodeLLDB


## Step2:Add “launch.json” and “tasks.json” in “.vscode” folder

```
prtos-hypervisor home directory
|-- .vscode
   |-- launch.json
   |-- tasks.json
|-- vscode-build
|-- core
|-- user
|......
```

Here is the content of `launch.json`:
```
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "prtospack check",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/user/tools/prtospack/prtospack",
      "args": [
        "check",
        "${workspaceFolder}/user/bail/examples/helloworld/prtos_cf.pef.prtos_conf",
        "-h",
        "${workspaceFolder}/core/prtos_core.pef:${workspaceFolder}/user/bail/examples/helloworld/prtos_cf.pef.prtos_conf"        
      ],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}/user/bail/examples/helloworld",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        },
        { "text": "set output-radix 16" }
      ],
      "preLaunchTask": "prtos-build"
    },
    {
      "name": "prtoseformat build",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/user/tools/pef/prtoseformat",
      "args": [
        "build",
        "${workspaceFolder}/core/prtos_core",
        "-o",
        "${workspaceFolder}/core/prtos_core.pef"        
      ],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}/core",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ],
      "preLaunchTask": "prtos-build"
    },
    {
      "name": "prtoscparser run",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/user/tools/prtoscparser/prtoscparser",
      "args": [
        "-o",
        "${workspaceFolder}/user/bail/examples/helloworld/prtos_cf.bin.prtos_conf",
        "-c",
        "${workspaceFolder}/user/bail/examples/helloworld/prtos_cf.aarch64.xml"        
      ],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}/user/bail/examples/helloworld",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ],
    "preLaunchTask": "prtos-build"
   },
   {
    "name": "PRTOS Core boot debug",
    "type": "cppdbg",
    "request": "launch",
    "miDebuggerPath":"/usr/bin/gdb-multiarch",
    "targetArchitecture": "arm",
    "program": "${workspaceFolder}/core/prtos_core",
    "setupCommands": [
        {"text": "target remote localhost:1234"},
        {"text": "set architecture aarch64"},
        {"text": "set output-radix 16" }
        {"text": "set remotetimeout 5"},
        {"text": "add-symbol-file ${workspaceFolder}/core/prtos_core 0x49007000"},
        {"text": "b start"},
        ],
    "launchCompleteCommand": "None",
    "externalConsole": false,
    "cwd": "${workspaceFolder}",
  },
  {
    "name": "rsw boot debug",
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/user/bail/examples/helloworld/resident_sw",
    "setupCommands": [
      {"text": "set architecture aarch64"},
      {"text": "set output-radix 16" }
      ],
    "cwd": "${workspaceFolder}",
    "MIMode": "gdb",
    "miDebuggerPath":"/usr/bin/gdb-multiarch",
    "miDebuggerServerAddress": "localhost:1234"
  },
  ]
}


```
Here is the content of `tasks.json`:

```
{
    "tasks": [
        {
            "label": "prtos-build",
            "command": "make",
            "args": ["-B"
            ],
            "options": {
                "cwd": "${workspaceFolder}/"
            },
            "dependsOn": [
                "make"
            ]
        }
    ],
    "version": "2.0.0"
}
```
When debugging prtos-hypervisor code, pls run the hypervisor on the qemu-system-aarch64 hypervisor.

Here is the demo picture to debug prtos core on qemu armv8 platform:
![prtos core debug on qemu armv8 platform](https://github.com/prtos-project/prtos-hypervisor/blob/main/doc/figures/prtos_core_debug_on_armv8.jpg)





