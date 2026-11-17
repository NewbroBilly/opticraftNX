$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$JavaHome = $env:JAVA_HOME
if (-not $JavaHome) { throw "Set JAVA_HOME to a JDK with jni.h and javac." }
$Jni = Join-Path $JavaHome "include"
$JniOs = Join-Path $Jni "win32"
$Out = Join-Path $Root "tests"
$Lib = Join-Path $Out "opti_buffer_test.dll"
$Compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $Compiler) { throw "g++ was not found. Use a MinGW-w64 toolchain or run the bash test script." }
& g++ -std=c++17 -Wall -Wextra -Werror -shared -I$Jni -I$JniOs `
  '-DOPTI_JNI_CLASS="OptiBufferBridgeTest"' `
  (Join-Path $Root "src\lwjgl_bufferutils_jni.cpp") -o $Lib
if ($LASTEXITCODE -ne 0) { throw "JNI bridge compilation failed." }
& javac (Join-Path $Out "java\OptiBufferBridgeTest.java")
if ($LASTEXITCODE -ne 0) { throw "javac failed." }
& java "-Dopti.bridge=$Lib" -cp (Join-Path $Out "java") OptiBufferBridgeTest
if ($LASTEXITCODE -ne 0) { throw "JNI bridge test failed." }
