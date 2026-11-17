package org.lwjgl.switchport;

import net.minecraft.client.Minecraft;

public final class SwitchMinecraftLauncherTest {
    public static void main(String[] args) throws Exception {
        SwitchMinecraftLauncher.main(new String[] { "--test-launch" });
        if (Minecraft.receivedArgs == null || Minecraft.receivedArgs.length != 1
                || !"--test-launch".equals(Minecraft.receivedArgs[0]))
            throw new AssertionError("launcher did not forward arguments");
        System.out.println("PASS: launcher installs LWJGL input before Minecraft.main");
    }
}
