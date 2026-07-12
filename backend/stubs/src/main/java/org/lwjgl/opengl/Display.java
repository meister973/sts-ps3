package org.lwjgl.opengl;

/** LWJGL2 Display shadow. Only CInputHelper reaches it (Display.isActive()
 * gates controller polling); the Vita app always has focus. */
public final class Display {

    public static boolean isActive() {
        return true;
    }

    private Display() {}
}
