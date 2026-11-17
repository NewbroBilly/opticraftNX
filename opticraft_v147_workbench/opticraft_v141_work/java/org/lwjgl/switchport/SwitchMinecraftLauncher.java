package org.lwjgl.switchport;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/** Entry point wrapper: installs the Horizon input backend before original Minecraft.main. */
public final class SwitchMinecraftLauncher {
    private SwitchMinecraftLauncher() {}

    public static void main(String[] args) throws Exception {
        SwitchInputBootstrap.install();
        Class<?> minecraft = Class.forName("net.minecraft.client.Minecraft");
        Method main = minecraft.getMethod("main", String[].class);
        try {
            main.invoke(null, new Object[] { args });
        } catch (InvocationTargetException e) {
            Throwable cause = e.getCause();
            if (cause instanceof Exception) throw (Exception) cause;
            if (cause instanceof Error) throw (Error) cause;
            throw new RuntimeException(cause);
        }
    }
}
