package org.lwjgl.switchport;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.IntBuffer;
import org.lwjgl.LWJGLException;
import org.lwjgl.opengl.InputImplementation;

/**
 * LWJGL 2.9.4 InputImplementation adapter.
 *
 * The Horizon main loop must call pump(...) once per frame with libnx Npad
 * state. Keyboard/Mouse then consume LWJGL-compatible state and event buffers.
 */
public final class SwitchInputImplementation implements InputImplementation {
    private static final int KEYBOARD_KEYS = 256;
    private static final int MOUSE_BUTTONS = 3;
    private static final int KEY_EVENT_BYTES = 18;
    private static final int MOUSE_EVENT_BYTES = 22;

    private final ByteBuffer keyboardState =
        ByteBuffer.allocateDirect(KEYBOARD_KEYS).order(ByteOrder.nativeOrder());
    private final ByteBuffer mouseState =
        ByteBuffer.allocateDirect(MOUSE_BUTTONS).order(ByteOrder.nativeOrder());
    private final ByteBuffer keyboardEvents =
        ByteBuffer.allocateDirect(4096).order(ByteOrder.nativeOrder());
    private final ByteBuffer mouseEvents =
        ByteBuffer.allocateDirect(4096).order(ByteOrder.nativeOrder());
    private final IntBuffer mouseCoords =
        ByteBuffer.allocateDirect(12).order(ByteOrder.nativeOrder()).asIntBuffer();

    private int width = 960, height = 544;
    private boolean keyboardCreated, mouseCreated, grabbed;
    private boolean horizonPollingEnabled;
    private int previousMouseX, previousMouseY;
    private int wheel;

    public SwitchInputImplementation() {}

    /** Called by the Horizon/libnx frame loop. Npad bit mapping is documented in input mapping. */
    public synchronized void pump(long buttons, int leftX, int leftY,
                                  int rightX, int rightY, int screenWidth, int screenHeight,
                                  int touchX, int touchY, boolean touchDown) {
        width = Math.max(1, screenWidth);
        height = Math.max(1, screenHeight);
        int[] events = SwitchInputBridge.update(buttons, leftX, leftY, rightX, rightY, width, height, touchX, touchY, touchDown);
        enqueueEvents(events);
    }

    /** Polls libnx Npad/touch state and feeds LWJGL queues. Call once at each game tick. */
    public synchronized void enableHorizonPolling(boolean enabled) {
        horizonPollingEnabled = enabled;
    }

    public synchronized void pumpFromHorizon(int screenWidth, int screenHeight) {
        width = Math.max(1, screenWidth);
        height = Math.max(1, screenHeight);
        enqueueEvents(SwitchInputBridge.pollFromHorizon(width, height));
    }

    private void enqueueEvents(int[] events) {
        if (events == null) return;
        long now = System.nanoTime();
        for (int i = 0; i + 3 < events.length; i += 4) {
            int type = events[i], code = events[i + 1], x = events[i + 2], y = events[i + 3];
            if (type == 1 || type == 2) {
                if (keyboardEvents.remaining() >= KEY_EVENT_BYTES) {
                    keyboardEvents.putInt(code & 0xff);
                    keyboardEvents.put((byte)(type == 1 ? 1 : 0));
                    keyboardEvents.putInt(characterForKey(code));
                    keyboardEvents.putLong(now);
                    keyboardEvents.put((byte)0);
                }
            } else if (type == 3 || type == 4) {
                if (mouseEvents.remaining() >= MOUSE_EVENT_BYTES) {
                    mouseEvents.put((byte)code);
                    mouseEvents.put((byte)(type == 3 ? 1 : 0));
                    mouseEvents.putInt(x);
                    mouseEvents.putInt(y);
                    mouseEvents.putInt(0);
                    mouseEvents.putLong(now);
                }
            } else if (type == 5) {
                int dx = x - previousMouseX;
                int dy = y - previousMouseY;
                if (mouseEvents.remaining() >= MOUSE_EVENT_BYTES) {
                    mouseEvents.put((byte)-1);
                    mouseEvents.put((byte)0);
                    mouseEvents.putInt(grabbed ? dx : x);
                    mouseEvents.putInt(grabbed ? dy : y);
                    mouseEvents.putInt(0);
                    mouseEvents.putLong(now);
                }
                previousMouseX = x;
                previousMouseY = y;
            } else if (type == 6) {
                wheel += code;
                if (mouseEvents.remaining() >= MOUSE_EVENT_BYTES) {
                    mouseEvents.put((byte)-1);
                    mouseEvents.put((byte)0);
                    mouseEvents.putInt(x);
                    mouseEvents.putInt(y);
                    mouseEvents.putInt(code);
                    mouseEvents.putLong(now);
                }
            }
        }
    }

    private static int characterForKey(int key) {
        if (key >= 2 && key <= 11) return key == 11 ? '0' : '1' + key - 2;
        if (key >= 16 && key <= 25) return "qwertyuiop".charAt(key - 16);
        if (key >= 30 && key <= 38) return "asdfghjkl".charAt(key - 30);
        if (key >= 44 && key <= 50) return "zxcvbnm".charAt(key - 44);
        if (key == 57) return ' ';
        if (key == 28) return 10;
        return 0;
    }

    @Override public boolean hasWheel() { return true; }
    @Override public int getButtonCount() { return MOUSE_BUTTONS; }
    @Override public void createMouse() { mouseCreated = true; }
    @Override public void destroyMouse() { mouseCreated = false; mouseEvents.clear(); }
    @Override public void createKeyboard() { keyboardCreated = true; }
    @Override public void destroyKeyboard() { keyboardCreated = false; keyboardEvents.clear(); }
    @Override public void grabMouse(boolean grab) { grabbed = grab; }
    @Override public int getNativeCursorCapabilities() { return 0; }
    @Override public void setCursorPosition(int x, int y) { previousMouseX=x; previousMouseY=y; }
    @Override public void setNativeCursor(Object cursor) throws LWJGLException {
        throw new LWJGLException("Native hardware cursors are not supported on Switch");
    }
    @Override public int getMinCursorSize() { return 0; }
    @Override public int getMaxCursorSize() { return 0; }
    @Override public Object createCursor(int w, int h, int xHot, int yHot, int numImages,
                                        IntBuffer images, IntBuffer delays) throws LWJGLException {
        throw new LWJGLException("Custom native cursors are not supported on Switch");
    }
    @Override public void destroyCursor(Object cursor) {}
    @Override public int getWidth() { return width; }
    @Override public int getHeight() { return height; }
    @Override public boolean isInsideWindow() { return true; }

    @Override public synchronized void pollKeyboard(ByteBuffer keyDown) {
        // LWJGL calls Keyboard.poll() from Minecraft's tick loop. Poll libnx here
        // so controller events enter the same queues as keyboard events.
        if (horizonPollingEnabled) pumpFromHorizon(width, height);
        for (int i=0; i<keyDown.capacity(); i++)
            keyDown.put(i, (byte)(SwitchInputBridge.isKeyDown(i) ? 1 : 0));
    }

    @Override public synchronized void readKeyboard(ByteBuffer destination) {
        keyboardEvents.flip();
        int count = Math.min(keyboardEvents.remaining(), destination.remaining());
        count -= count % KEY_EVENT_BYTES;
        for (int i=0; i<count; i++) destination.put(keyboardEvents.get());
        keyboardEvents.compact();
    }

    @Override public synchronized void pollMouse(IntBuffer coords, ByteBuffer buttons) {
        int x=SwitchInputBridge.getMouseX(), y=SwitchInputBridge.getMouseY();
        if (coords.remaining() >= 3) {
            coords.put(0, x); coords.put(1, y); coords.put(2, wheel);
        }
        for (int i=0; i<buttons.capacity(); i++)
            buttons.put(i, (byte)(SwitchInputBridge.isMouseDown(i) ? 1 : 0));
        wheel=0;
    }

    @Override public synchronized void readMouse(ByteBuffer destination) {
        mouseEvents.flip();
        int count = Math.min(mouseEvents.remaining(), destination.remaining());
        count -= count % MOUSE_EVENT_BYTES;
        for (int i=0; i<count; i++) destination.put(mouseEvents.get());
        mouseEvents.compact();
    }
}
