# OptiCraft Switch v126 — validation checkpoint

## Original Minecraft 1.2.5 JAR probe (host)

Executed against the user-provided `/mnt/data/minecraft.jar`:

```text
JAR_CLASSES_REGISTERED=1152
JAR_RESOURCES_REGISTERED=414
MAIN_EXECUTION=RETURNED
INSTRUCTIONS_EXECUTED=891573
MAIN_RETURNED
AWT_WINDOW_CREATES=1
AWT_WINDOW_TITLE=Minecraft
AWT_WINDOW_SIZE=854x480
AWT_WINDOW_VISIBLE=true
ORIGINAL_MAIN_AWT_HOST_HANDOFF=PASS
```

This confirms that the current host-side interpreter can load the original archive's classes/resources, execute the `Minecraft.main(String[])` path to completion, and issue the AWT window callbacks to a host mock.

## Additional individual tests observed passing

- JVM heap/object/array/static-root/mark-sweep
- class runtime: registration, inheritance, dispatch, assignability and field layouts
- exception table lookup: typed catch, inheritance, catch-all and PC ranges
- class Code parser + interpreter
- object bytecode interpreter: objects, arrays, wide locals, stack ops, instanceof and athrow handler
- Java language intrinsics: StringBuilder, String constants, currentTimeMillis and long remainder
- bytecode structural verifier
- class linker preparation
- original classfile inspection: 1,152 classes, 9,112 methods with Code, 633 exception handlers, 9,112 bytecode-verified methods
- AWT compatibility: Frame/Canvas/BorderLayout/Dimension -> host window callbacks

## Validation caveat

The aggregate `run_integrated_runtime_suite.sh` did not complete in this environment: it stalled at `run_jvm_integrated_invocation_test.sh` and was terminated by the execution timeout. Do not interpret the individual passing tests as an all-green aggregate suite.

## What this is not

- Not a Nintendo Switch NRO build.
- Not a hardware test on Switch / Atmosphere.
- Not a rendered Minecraft world.
- Not a complete LWJGL/OpenGL implementation.
- Not proof that the game's worker thread is scheduled correctly: `Thread.start` is still a compatibility stub and does not launch an independent thread.

The AWT handoff is currently tested against host callbacks only. The next substantial engineering block is to connect the JVM runtime and its AWT callbacks to the libnx window/framebuffer lifecycle, then implement a real thread/event loop and route LWJGL rendering calls into the Switch graphics backend.
