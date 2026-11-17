# OptiCraft Switch v139 — LWJGL / OpenGL Compatibility Bridge

## Что добавлено относительно v138

Создан `LwjglGL11Bridge` — единая host-side точка совместимости LWJGL 2 / OpenGL 1.1, которая сводит вызовы к общему `LegacyGLContext`:

- immediate mode: `glBegin/glEnd`, `glColor*`, `glTexCoord2f`, `glNormal3f`, `glVertex3f`;
- fixed-function матрицы: `glMatrixMode`, `glLoadIdentity`, `glPushMatrix`, `glPopMatrix`, `glTranslatef`, `glRotatef`, `glScalef`, `glOrtho`;
- состояние: `glEnable/glDisable`, viewport, clear color/depth, clear masks, depth func, blend func, alpha func;
- текстуры: gen/delete/bind, параметры, `glTexImage2D`, `glTexSubImage2D`, pixel-store;
- client arrays: vertex/color/normal/texcoord pointers, enable/disable, draw arrays/elements;
- запросы: `glGetError`, `glGetString`, `glGetInteger`, `glGetFloat`, `glIsEnabled`;
- `glReadPixels`-подобное чтение RGBA8 из software framebuffer;
- Display swap/update handoff через callback `set_present_callback`, чтобы platform layer мог отправить готовый framebuffer в Horizon/libnx.

Дополнительно исправлена семантика `glTexImage2D(..., pixels=null)`: теперь выделяется пустое хранилище текстуры, а не возвращается ошибка.

## Проверка

`tests/run_lwjgl_graphics_suite.sh` запускает 7 независимых host-тестов:

1. GL compatibility core;
2. texture API;
3. client arrays;
4. legacy GL context;
5. software renderer texture sampling;
6. framebuffer blit;
7. новый LWJGL GL11 bridge.

Все 7 прошли в текущем host-окружении.

## Что это НЕ означает

Это ещё не «полный LWJGL/OpenGL» и не запуск Minecraft. Bridge — C++ facade; JNI native entrypoints LWJGL ещё не зарегистрированы и не подключены к Java `org.lwjgl.opengl.GL11`. Передача `ByteBuffer`/`IntBuffer` из JVM в native pointers не интегрирована. Также не завершены:

- GL13/ARB multitexture и независимые texture units;
- VBO/ARB_vertex_buffer_object, display lists и occlusion queries;
- полный fixed-function lighting/material/fog/texgen/logic-op/polygon-offset/culling semantics;
- все перегрузки NIO buffer/pointer API, pixel formats и readback;
- LWJGL `Display`, `Pbuffer`, `Drawable`, `ContextCapabilities`, GLU wrappers;
- JNI registration/loading для оригинального `lwjgl.jar`;
- аппаратный backend Switch (deko3d/NVN-compatible path) и GPU shader pipeline.

Текущий renderer — CPU software rasterizer. Он нужен как проверяемый reference backend и не является финальным высокопроизводительным рендерером для Minecraft на Switch. Функции вроде lighting/fog, VBO и multi-texture нельзя считать реализованными только потому, что их классы есть в ABI inventory.

## Сборка Switch

NRO не собран. Для этого архива не было доступного подтверждённого devkitPro/libnx toolchain в текущем окружении. Все PASS выше относятся только к host C++ тестам.

## Следующий крупный блок v140

1. Реальные JNI entrypoints для GL11 с безопасным чтением direct NIO buffers.
2. Привязка `Display.create/update/swapBuffers/destroy` к Switch window/framebuffer lifecycle.
3. Полный список используемых Minecraft методов из ABI inventory — каждую запись классифицировать implemented / wrapper / unsupported.
4. GL13 multitexture и texture-unit state.
5. VBO path и базовые display lists.
6. Затем аппаратный Switch renderer; CPU renderer оставить fallback/reference.
