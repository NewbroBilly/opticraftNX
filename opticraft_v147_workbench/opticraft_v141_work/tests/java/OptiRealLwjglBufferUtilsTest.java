import java.nio.*;
import java.lang.reflect.*;

public final class OptiRealLwjglBufferUtilsTest {
    public static void main(String[] args) throws Exception {
        System.load(System.getProperty("opti.bridge"));
        ByteBuffer bytes = ByteBuffer.allocateDirect(24);
        for (int i=0;i<bytes.capacity();i++) bytes.put(i,(byte)0x6B);
        bytes.position(5); bytes.limit(13);
        org.lwjgl.BufferUtils.zeroBuffer(bytes);
        bytes.limit(bytes.capacity());
        for (int i=0;i<24;i++) {
            int expected=(i>=5 && i<13)?0:0x6B;
            if ((bytes.get(i)&255)!=expected) throw new AssertionError("byte range "+i);
        }
        Method address=org.lwjgl.BufferUtils.class.getDeclaredMethod("getBufferAddress",Buffer.class);
        address.setAccessible(true);
        long ptr=(Long)address.invoke(null,bytes);
        if (ptr==0) throw new AssertionError("null direct-buffer address");

        IntBuffer ints=ByteBuffer.allocateDirect(32).order(ByteOrder.nativeOrder()).asIntBuffer();
        for(int i=0;i<ints.capacity();i++) ints.put(i,0x12345678);
        ints.position(2); ints.limit(5);
        org.lwjgl.BufferUtils.zeroBuffer(ints);
        ints.limit(ints.capacity());
        for(int i=0;i<ints.capacity();i++) {
            int expected=(i>=2 && i<5)?0:0x12345678;
            if(ints.get(i)!=expected) throw new AssertionError("int range "+i);
        }
        System.out.println("Original LWJGL BufferUtils JNI ABI: PASS");
    }
}
