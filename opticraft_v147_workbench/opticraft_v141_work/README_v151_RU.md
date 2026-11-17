# OptiCraft Switch v151 — SD Java archive loader

New libnx target loads the original Minecraft JAR from `sdmc:/switch/opticraft/minecraft.jar`, extracts `net/minecraft/client/Minecraft.class`, validates its class header and reports the `main(String[])` method.

This is a class loading gate only. It does not execute Minecraft bytecode and is not a game build.
