package com.badlogic.gdx.utils;

/** Shadow: gdx2d/box2d natives are statically linked (or replaced) in the
 * Vita build; there is nothing to load at runtime. */
public class GdxNativesLoader {

    public static boolean disableNativesLoading = true;

    public static synchronized void load() {
    }
}
