package com.stsvita.backend;

import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import java.nio.ShortBuffer;
import org.teavm.interop.Address;

/**
 * Native address of a NIO buffer for the @Import boundary.
 *
 * Stock TeaVM 0.15 prunes the internal interface that would expose a direct
 * buffer's malloc address (and gdx-teavm's TNativeBufferUtil path relies on a
 * TeaVM fork), so all backend/GL buffers are heap-array-backed and we take
 * the backing array's address via Address.ofData:
 *   - hasArray(): address into the backing array. Held live by the buffer;
 *     safe for synchronous calls (glBufferData/glTexImage2D).
 *   - typed view without an accessible array (VertexArray's
 *     byteBuffer.asFloatBuffer()): copy the remaining elements into a fresh
 *     array. Client-side vertex arrays are read at DRAW time, after
 *     glVertexAttribPointer returns, so the copy is PINNED until the frame
 *     ends (VitaApplication.endFrame -> clearPins), preventing GC collection
 *     or relocation from invalidating the pointer mid-frame.
 *
 * GL only reads buffers here; the few GL calls that write into a buffer
 * (the glGet and glGen family) use VitaGL20's bounce-array path, not this.
 */
public final class Buffers {

    private static Object[] pins = new Object[256];
    private static int pinCount;

    /** Released at end of frame by VitaApplication. */
    public static void clearPins() {
        for (int i = 0; i < pinCount; i++) {
            pins[i] = null;
        }
        pinCount = 0;
    }

    public static Address addr(Buffer buffer) {
        if (buffer == null) {
            return Address.fromInt(0);
        }
        if (buffer instanceof ByteBuffer) {
            ByteBuffer b = (ByteBuffer) buffer;
            if (b.hasArray()) {
                return Address.ofData(b.array()).add(b.arrayOffset() + b.position());
            }
            byte[] d = drain(b);
            pin(d);
            return Address.ofData(d);
        }
        if (buffer instanceof FloatBuffer) {
            FloatBuffer b = (FloatBuffer) buffer;
            if (b.hasArray()) {
                return Address.ofData(b.array()).add((b.arrayOffset() + b.position()) << 2);
            }
            float[] d = new float[b.remaining()];
            int p = b.position();
            b.get(d);
            b.position(p);
            pin(d);
            return Address.ofData(d);
        }
        if (buffer instanceof IntBuffer) {
            IntBuffer b = (IntBuffer) buffer;
            if (b.hasArray()) {
                return Address.ofData(b.array()).add((b.arrayOffset() + b.position()) << 2);
            }
            int[] d = new int[b.remaining()];
            int p = b.position();
            b.get(d);
            b.position(p);
            pin(d);
            return Address.ofData(d);
        }
        if (buffer instanceof ShortBuffer) {
            ShortBuffer b = (ShortBuffer) buffer;
            if (b.hasArray()) {
                return Address.ofData(b.array()).add((b.arrayOffset() + b.position()) << 1);
            }
            short[] d = new short[b.remaining()];
            int p = b.position();
            b.get(d);
            b.position(p);
            pin(d);
            return Address.ofData(d);
        }
        if (buffer instanceof LongBuffer) {
            LongBuffer b = (LongBuffer) buffer;
            if (b.hasArray()) {
                return Address.ofData(b.array()).add((b.arrayOffset() + b.position()) << 3);
            }
        }
        if (buffer instanceof DoubleBuffer) {
            DoubleBuffer b = (DoubleBuffer) buffer;
            if (b.hasArray()) {
                return Address.ofData(b.array()).add((b.arrayOffset() + b.position()) << 3);
            }
        }
        throw new IllegalArgumentException("unsupported buffer type: " + buffer);
    }

    private static void pin(Object object) {
        if (pinCount == pins.length) {
            Object[] next = new Object[pins.length << 1];
            for (int i = 0; i < pins.length; i++) {
                next[i] = pins[i];
            }
            pins = next;
        }
        pins[pinCount++] = object;
    }

    /** Element shift derived from runtime type; arg kept for call-site clarity. */
    public static Address addr(Buffer buffer, int elemShift) {
        return addr(buffer);
    }

    private static byte[] drain(ByteBuffer b) {
        byte[] d = new byte[b.remaining()];
        int p = b.position();
        b.get(d);
        b.position(p);
        return d;
    }

    private Buffers() {}
}
