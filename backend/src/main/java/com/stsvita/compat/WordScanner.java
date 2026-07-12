package com.stsvita.compat;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.Reader;
import java.nio.charset.StandardCharsets;

/**
 * Replacement target for the game's java.util.Scanner uses. TeaVM's classlib
 * has no Scanner, and the host-JDK fallback drags in locale providers and
 * NIO channels. The game only tokenizes strings/small files, so this covers
 * the entire surface it calls (verified against all 9 call sites):
 * Scanner(String), Scanner(File), hasNext, next, hasNextLine, nextLine,
 * close. References are retargeted here at build time by ClasslibShims.
 */
public final class WordScanner {

    private final String text;
    private int pos;

    public WordScanner(String source) {
        this.text = source == null ? "" : source;
    }

    public WordScanner(File file) throws IOException {
        StringBuilder sb = new StringBuilder();
        try (Reader r = new InputStreamReader(
                new FileInputStream(file), StandardCharsets.UTF_8)) {
            char[] buf = new char[4096];
            int n;
            while ((n = r.read(buf)) >= 0) {
                sb.append(buf, 0, n);
            }
        }
        this.text = sb.toString();
    }

    public boolean hasNext() {
        return skipWhitespace() < text.length();
    }

    public String next() {
        int start = skipWhitespace();
        if (start >= text.length()) {
            throw new java.util.NoSuchElementException();
        }
        int end = start;
        while (end < text.length() && !Character.isWhitespace(text.charAt(end))) {
            end++;
        }
        pos = end;
        return text.substring(start, end);
    }

    public boolean hasNextLine() {
        return pos < text.length();
    }

    public String nextLine() {
        if (pos >= text.length()) {
            throw new java.util.NoSuchElementException();
        }
        int nl = text.indexOf('\n', pos);
        String line;
        if (nl < 0) {
            line = text.substring(pos);
            pos = text.length();
        } else {
            line = text.substring(pos, nl);
            pos = nl + 1;
        }
        return line.endsWith("\r") ? line.substring(0, line.length() - 1) : line;
    }

    public void close() {
    }

    private int skipWhitespace() {
        int p = pos;
        while (p < text.length() && Character.isWhitespace(text.charAt(p))) {
            p++;
        }
        pos = p;
        return p;
    }
}
