package org.lwjgl.switchport;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Field;
import java.util.Map;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.lwjgl.LWJGLException;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;
import org.lwjgl.opengl.InputImplementation;

/** Installs the Switch implementation before the original Minecraft main method. */
public final class SwitchInputBootstrap {
    private SwitchInputBootstrap() {}

    public static SwitchInputImplementation install() throws LWJGLException {
        SwitchInputImplementation implementation = new SwitchInputImplementation();
        installKeyboard(implementation);
        installMouse(implementation);
        implementation.enableHorizonPolling(true);
        return implementation;
    }

    private static void installKeyboard(SwitchInputImplementation implementation)
            throws LWJGLException {
        try {
            prepareWithoutHostSys(Keyboard.class);
            Method create = Keyboard.class.getDeclaredMethod("create", InputImplementation.class);
            create.setAccessible(true);
            create.invoke(null, implementation);
        } catch (InvocationTargetException e) {
            throw wrap("Keyboard", e.getCause());
        } catch (Exception e) {
            throw wrap("Keyboard", e);
        }
    }

    private static void installMouse(SwitchInputImplementation implementation)
            throws LWJGLException {
        try {
            prepareWithoutHostSys(Mouse.class);
            implementation.createMouse();
            setStatic(Mouse.class, "implementation", implementation);
            setStatic(Mouse.class, "hasWheel", Boolean.TRUE);
            setStatic(Mouse.class, "buttonCount", Integer.valueOf(implementation.getButtonCount()));
            setStatic(Mouse.class, "buttons",
                      ByteBuffer.allocateDirect(implementation.getButtonCount()));
            setStatic(Mouse.class, "coord_buffer",
                      ByteBuffer.allocateDirect(12).order(ByteOrder.nativeOrder()).asIntBuffer());
            ByteBuffer events = ByteBuffer.allocate(1100).order(ByteOrder.nativeOrder());
            events.limit(0);
            setStatic(Mouse.class, "readBuffer", events);
            setStatic(Mouse.class, "created", Boolean.TRUE);
        } catch (Exception e) {
            throw wrap("Mouse", e);
        }
    }

    private static void setStatic(Class<?> owner, String name, Object value)
            throws Exception {
        Field field = owner.getDeclaredField(name);
        field.setAccessible(true);
        field.set(null, value);
    }

    private static LWJGLException wrap(String component, Throwable cause) {
        return new LWJGLException("Unable to install Switch input into " + component, cause);
    }

    // LWJGL's default initialize() calls Sys.initialize(), which selects a desktop
    // AWT implementation. Prepare the small static state locally so the original
    // Keyboard/Mouse classes can use the Switch InputImplementation without desktop Sys.
    @SuppressWarnings("unchecked")
    private static void prepareWithoutHostSys(Class<?> owner) throws Exception {
        Field initialized = owner.getDeclaredField("initialized");
        initialized.setAccessible(true);
        if (initialized.getBoolean(null)) return;

        if (owner == Keyboard.class) {
            initialized.setBoolean(null, true);
            return;
        }
        if (owner == Mouse.class) {
            Field namesField = owner.getDeclaredField("buttonName");
            namesField.setAccessible(true);
            String[] names = new String[16];
            Field mapField = owner.getDeclaredField("buttonMap");
            mapField.setAccessible(true);
            Map<String, Integer> map = (Map<String, Integer>) mapField.get(null);
            for (int i = 0; i < names.length; i++) {
                names[i] = "BUTTON" + i;
                map.put(names[i], Integer.valueOf(i));
            }
            namesField.set(null, names);
            initialized.setBoolean(null, true);
        }
    }
}
