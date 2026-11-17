package net.minecraft.client;

import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;

/** Test-only stand-in for the original Minecraft main class. */
public final class Minecraft {
    public static String[] receivedArgs;
    public static void main(String[] args) {
        if (!Keyboard.isCreated() || !Mouse.isCreated())
            throw new AssertionError("Switch input must be installed before Minecraft.main");
        receivedArgs = args;
    }
}
