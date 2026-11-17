# OptiCraft Switch v150

Host-runtime fix: the Switch host adapter now accepts OptiFine native cursor construction as an empty cursor. The original client no longer logs LWJGLException: Native cursors not supported during startup.

The host probe still reaches the real Minecraft 1.2.5 menu and emits 30 frames at the 30M-instruction cap. This is not a full game NRO and has not been run on a physical Switch. See [REPORT_v150_NATIVE_CURSOR_RU.md](REPORT_v150_NATIVE_CURSOR_RU.md).
