import java.nio.*;

public final class OptiBufferBridgeTest {
    static { System.load(System.getProperty("opti.bridge")); }
    private static native void zeroBuffer0(Buffer buffer, long offset, long length);
    private static native long getBufferAddress(Buffer buffer);

    public static void main(String[] args) {
        ByteBuffer bytes = ByteBuffer.allocateDirect(32);
        for (int i = 0; i < bytes.capacity(); i++) bytes.put(i, (byte)0x5A);
        zeroBuffer0(bytes, 4, 8);
        for (int i = 0; i < 32; i++) {
            int expected = (i >= 4 && i < 12) ? 0 : 0x5A;
            if ((bytes.get(i) & 0xFF) != expected)
                throw new AssertionError("ByteBuffer mismatch at " + i);
        }
        if (getBufferAddress(bytes) == 0)
            throw new AssertionError("direct buffer address is null");

        IntBuffer ints = ByteBuffer.allocateDirect(40).order(ByteOrder.nativeOrder()).asIntBuffer();
        for (int i = 0; i < ints.capacity(); i++) ints.put(i, 0x12345678);
        zeroBuffer0(ints, 4, 8); // byte offset, not element offset
        for (int i = 0; i < ints.capacity(); i++) {
            int expected = (i == 1 || i == 2) ? 0 : 0x12345678;
            if (ints.get(i) != expected)
                throw new AssertionError("IntBuffer mismatch at " + i);
        }
        boolean rejected = false;
        try { zeroBuffer0(bytes, 30, 8); }
        catch (IndexOutOfBoundsException expected) { rejected = true; }
        if (!rejected) throw new AssertionError("out-of-range zero was not rejected");
        System.out.println("BufferUtils JNI bridge tests: PASS");
    }
}
