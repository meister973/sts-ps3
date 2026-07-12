package com.stsvita.backend;

import java.nio.charset.StandardCharsets;
import org.teavm.interop.Address;

/** Java<->C string marshalling for the @Import boundary. */
public final class CStrings {

    public static byte[] toC(String s) {
        byte[] utf8 = s.getBytes(StandardCharsets.UTF_8);
        byte[] c = new byte[utf8.length + 1];
        System.arraycopy(utf8, 0, c, 0, utf8.length);
        return c;
    }

    public static String fromC(Address a) {
        if (a.toInt() == 0) {
            return null;
        }
        int len = 0;
        while (a.add(len).getByte() != 0) {
            len++;
        }
        byte[] bytes = new byte[len];
        for (int i = 0; i < len; i++) {
            bytes[i] = a.add(i).getByte();
        }
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private CStrings() {}
}
