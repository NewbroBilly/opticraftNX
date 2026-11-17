package org.lwjgl.switchport;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.IntBuffer;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;

public final class SwitchInputImplementationTest {
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        SwitchInputImplementation input = SwitchInputBootstrap.install();
        if (!Keyboard.isCreated() || !Mouse.isCreated())
            throw new AssertionError("Switch InputImplementation was not installed into LWJGL");
        Keyboard.poll(); // exercises the real LWJGL per-tick polling entry point
        input.enableHorizonPolling(false); // host test drives deterministic snapshots below
        input.createKeyboard();
        input.createMouse();

        input.pump(0L, 0, 20000, 0, 0, 960, 544, 0, 0, false);
        ByteBuffer keys = ByteBuffer.allocateDirect(256);
        input.pollKeyboard(keys);
        if (keys.get(17) != 1) throw new AssertionError("W key state not polled");

        ByteBuffer keyEvents = ByteBuffer.allocateDirect(4096).order(ByteOrder.nativeOrder());
        input.readKeyboard(keyEvents);
        keyEvents.flip();
        if (keyEvents.remaining() < 18) throw new AssertionError("missing keyboard event");
        if (keyEvents.getInt() != 17 || keyEvents.get() != 1)
            throw new AssertionError("invalid LWJGL keyboard event layout");

        input.pump(1L << 9, 0, 0, 0, 0, 960, 544, 0, 0, false); // ZR -> left mouse
        ByteBuffer buttons = ByteBuffer.allocateDirect(3);
        IntBuffer coords = ByteBuffer.allocateDirect(12).order(ByteOrder.nativeOrder()).asIntBuffer();
        input.pollMouse(coords, buttons);
        if (buttons.get(0) != 1) throw new AssertionError("left mouse state not polled");

        ByteBuffer mouseEvents = ByteBuffer.allocateDirect(4096).order(ByteOrder.nativeOrder());
        input.readMouse(mouseEvents);
        mouseEvents.flip();
        if (mouseEvents.remaining() < 22) throw new AssertionError("missing mouse event");
        if (mouseEvents.get() != 0 || mouseEvents.get() != 1)
            throw new AssertionError("invalid LWJGL mouse event layout");

        // L shoulder scrolls one hotbar step backwards using LWJGL wheel units.
        input.pump(1L << 6, 0, 0, 0, 0, 960, 544, 0, 0, false);
        input.pollMouse(coords, buttons);
        if (coords.get(2) != 120) throw new AssertionError("L shoulder wheel tick missing");
        input.pump(1L << 7, 0, 0, 0, 0, 960, 544, 0, 0, false);
        input.pollMouse(coords, buttons);
        if (coords.get(2) != -120) throw new AssertionError("R shoulder wheel tick missing");

        System.out.println("PASS: LWJGL InputImplementation Java adapter");
    }
}
