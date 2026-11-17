# OptiCraft Switch v149

Host-runtime fix: fast texture uploads now classify Minecraft texture atlases by the active resource path, because RenderEngine assigns its atlas IDs only after upload returns.

See [REPORT_v149_ATLAS_CLASSIFICATION_RU.md](REPORT_v149_ATLAS_CLASSIFICATION_RU.md). This source checkpoint is not a full game NRO and has not been run on a physical Switch.
