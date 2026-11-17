package org.lwjgl;
public final class OptiSysNativeTest {
    static final class TestSys extends DefaultSysImplementation {
        public long getTime() { return 0L; }
        public void alert(String title, String message) {}
        public String getClipboard() { return ""; }
        public boolean openURL(String url) { return false; }
        public int getRequiredJNIVersion() { return 19; }
    }
    public static void main(String[] args) {
        System.load(args[0]);
        TestSys sys = new TestSys();
        if (sys.getJNIVersion() != 19)
            throw new AssertionError("JNI ABI version mismatch: " + sys.getJNIVersion());
        int expected = Long.SIZE / 8;
        if (sys.getPointerSize() != expected)
            throw new AssertionError("pointer size mismatch: " + sys.getPointerSize());
        sys.setDebug(false); sys.setDebug(true); sys.setDebug(false);
        System.out.println("PASS: Sys JNI ABI; version=" + sys.getJNIVersion()
                           + ", pointerSize=" + sys.getPointerSize());
    }
}
