package org.lwjgl.switchport;
public final class SwitchInputBridgeTest {
    public static void main(String[] args) {
        System.load(args[0]);
        SwitchInputBridge.reset();
        int[] platformNeutral = SwitchInputBridge.pollFromHorizon(960, 544);
        if (platformNeutral.length != 0) throw new AssertionError("host platform poll must be neutral");
        int[] none = SwitchInputBridge.update(0L, 0, 0, 0, 0, 960, 544, 0, 0, false);
        if (none.length != 0) throw new AssertionError("unexpected initial events");
        int[] down = SwitchInputBridge.update(0L, 0, 20000, 0, 0, 960, 544, 0, 0, false);
        if (down.length != 4 || down[0] != 1 || down[1] != 17)
            throw new AssertionError("expected W key-down event");
        if (!SwitchInputBridge.isKeyDown(17)) throw new AssertionError("W state missing");
        int[] held = SwitchInputBridge.update(0L, 0, 20000, 0, 0, 960, 544, 0, 0, false);
        if (held.length != 0) throw new AssertionError("duplicate key-down");
        int[] release = SwitchInputBridge.update(0L, 0, 0, 0, 0, 960, 544, 0, 0, false);
        if (release.length != 4 || release[0] != 2)
            throw new AssertionError("expected key-up");
        int[] move = SwitchInputBridge.update(0L, 0, 0, 20000, 10000, 960, 544, 0, 0, false);
        if (move.length != 4 || move[0] != 5)
            throw new AssertionError("expected mouse motion");
        SwitchInputBridge.update(1L << 9, 0, 0, 0, 0, 960, 544, 0, 0, false); // ZR -> attack/left mouse
        if (!SwitchInputBridge.isMouseDown(0)) throw new AssertionError("left mouse state missing");
        int[] touchDown = SwitchInputBridge.update(0L, 0, 0, 0, 0, 960, 544, 320, 200, true);
        if (!SwitchInputBridge.isMouseDown(0)) throw new AssertionError("touch press missing");
        if (SwitchInputBridge.getMouseX() != 320 || SwitchInputBridge.getMouseY() != 200)
            throw new AssertionError("touch coordinates missing");
        SwitchInputBridge.update(0L, 0, 0, 0, 0, 960, 544, 320, 200, false);
        if (SwitchInputBridge.isMouseDown(0)) throw new AssertionError("touch release missing");
        SwitchInputBridge.reset();
        if (SwitchInputBridge.isMouseDown(0)) throw new AssertionError("reset failed");
        System.out.println("PASS: Switch input JNI event bridge");
    }
}
