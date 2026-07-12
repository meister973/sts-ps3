package com.badlogic.gdx.graphics.glutils;

import com.badlogic.gdx.Application;

/**
 * TeaVM-safe replacement for libGDX 1.9.5 GLVersion.
 *
 * The stock class uses java.util.regex + String.split while Graphics.init()
 * is still bringing the backend up. That is unnecessary here and has already
 * faulted on Vita before the game reaches create(). Keep the public API, parse
 * the first version-looking token manually, and default to 2.0.0.
 */
public class GLVersion {

    public enum Type {
        OpenGL,
        GLES,
        WebGL,
        NONE
    }

    private final Type type;
    private final String vendorString;
    private final String rendererString;
    private int majorVersion = 2;
    private int minorVersion = 0;
    private int releaseVersion = 0;

    public GLVersion(Application.ApplicationType appType, String versionString,
                     String vendorString, String rendererString) {
        if (appType == Application.ApplicationType.Android
                || appType == Application.ApplicationType.iOS) {
            type = Type.GLES;
        } else if (appType == Application.ApplicationType.WebGL) {
            type = Type.WebGL;
        } else if (appType == Application.ApplicationType.Desktop
                || appType == Application.ApplicationType.Applet) {
            type = Type.OpenGL;
        } else {
            type = Type.NONE;
            majorVersion = minorVersion = releaseVersion = -1;
        }
        this.vendorString = vendorString == null ? "" : vendorString;
        this.rendererString = rendererString == null ? "" : rendererString;
        if (type != Type.NONE) {
            parse(versionString);
        }
    }

    private void parse(String versionString) {
        if (versionString == null) {
            return;
        }
        int start = -1;
        for (int i = 0; i < versionString.length(); i++) {
            char c = versionString.charAt(i);
            if (c >= '0' && c <= '9') {
                start = i;
                break;
            }
        }
        if (start < 0) {
            return;
        }
        int[] out = {2, 0, 0};
        int part = 0;
        int value = 0;
        boolean hasDigits = false;
        for (int i = start; i <= versionString.length(); i++) {
            char c = i < versionString.length() ? versionString.charAt(i) : '\0';
            if (c >= '0' && c <= '9') {
                hasDigits = true;
                value = value * 10 + (c - '0');
            } else {
                if (hasDigits && part < out.length) {
                    out[part++] = value;
                    value = 0;
                    hasDigits = false;
                }
                if (c != '.' || part >= out.length) {
                    break;
                }
            }
        }
        majorVersion = out[0];
        minorVersion = out[1];
        releaseVersion = out[2];
    }

    public Type getType() {
        return type;
    }

    public int getMajorVersion() {
        return majorVersion;
    }

    public int getMinorVersion() {
        return minorVersion;
    }

    public int getReleaseVersion() {
        return releaseVersion;
    }

    public String getVendorString() {
        return vendorString;
    }

    public String getRendererString() {
        return rendererString;
    }

    public boolean isVersionEqualToOrHigher(int testMajorVersion, int testMinorVersion) {
        return majorVersion > testMajorVersion
                || (majorVersion == testMajorVersion && minorVersion >= testMinorVersion);
    }

    public String getDebugVersionString() {
        return "Type: " + type
                + "\nVersion: " + majorVersion + ":" + minorVersion + ":" + releaseVersion
                + "\nVendor: " + vendorString
                + "\nRenderer: " + rendererString;
    }
}
