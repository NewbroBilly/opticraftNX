package org.lwjgl.switchport;

/**
 * Native event bridge used by the future Switch InputImplementation.
 * update() returns packed quadruples: type, key/button code, mouse X, mouse Y.
 * Types: 1 key-down, 2 key-up, 3 mouse-down, 4 mouse-up, 5 mouse-motion.
 */
public final class SwitchInputBridge {
    private SwitchInputBridge() {}
    public static native int[] pollFromHorizon(int screenWidth, int screenHeight);
    public static native int[] update(long buttons, int leftX, int leftY,
                                      int rightX, int rightY,
                                      int screenWidth, int screenHeight,
                                      int touchX, int touchY, boolean touchDown);
    public static native boolean isKeyDown(int lwjglKey);
    public static native boolean isMouseDown(int button);
    public static native int getMouseX();
    public static native int getMouseY();
    public static native void reset();
}
