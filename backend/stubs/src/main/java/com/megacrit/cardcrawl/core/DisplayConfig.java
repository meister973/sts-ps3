package com.megacrit.cardcrawl.core;

/**
 * Shadow of the game's display-config reader (third game-package shadow;
 * see AsyncSaver for the policy). Justification: the original is desktop
 * display-mode negotiation via java.util.Scanner — which TeaVM cannot host —
 * and on a console every value it negotiates is a hardware constant.
 * Public surface matches the original; getters return the PS3's 720p output,
 * which must match VitaApplication.Config or layout and framebuffer disagree.
 */
public class DisplayConfig {

    public static DisplayConfig readConfig() {
        return new DisplayConfig();
    }

    public static void writeDisplayConfigFile(int w, int h, int fps,
                                              boolean fs, boolean wfs, boolean vs) {
    }

    public int getWidth() {
        return 1280;
    }

    public int getHeight() {
        return 720;
    }

    public int getMaxFPS() {
        return 60;
    }

    public boolean getIsFullscreen() {
        // Reported as windowed, not fullscreen: the options screen's
        // getFullScreenOnlyResolutions() enumerates display modes and filters
        // out anything under 1024x576 — with the Vita's single 960x544 mode
        // that yields an EMPTY resolution dropdown and a null-selectionBox NPE.
        // The windowed path always includes the current screen resolution, so
        // the dropdown has a valid entry. Rendering is our fixed framebuffer
        // regardless of this flag.
        return false;
    }

    public boolean getWFS() {
        return false;
    }

    public boolean getIsVsync() {
        return true;
    }

    @Override
    public String toString() {
        return "1280x720 @60 fullscreen vsync (PS3)";
    }
}
