package com.badlogic.gdx.utils;

import com.badlogic.gdx.math.Matrix3;
import com.badlogic.gdx.math.Matrix4;
import com.stsvita.backend.Buffers;
import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.CharBuffer;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import java.nio.ShortBuffer;
import java.util.ArrayList;
import org.teavm.interop.Address;
import org.teavm.interop.Import;
import org.teavm.interop.c.Include;

/**
 * Shadow of libGDX's BufferUtils with the JNI layer replaced by TeaVM
 * interop: buffers are TeaVM direct buffers (malloc-backed on the C target),
 * copies are real memcpy through glue, transforms/finds are plain Java.
 * Public semantics match 1.9.5 exactly (offsets in elements on the array
 * side, buffer position as destination offset, limit updates as documented
 * there); "unsafe" buffers are ordinary direct buffers plus bookkeeping.
 */
@Include("stsv_misc.h")
public final class BufferUtils {

    private static final ArrayList<ByteBuffer> unsafeBuffers = new ArrayList<>();
    private static int allocatedUnsafe;

    /* ---- allocation ----
     * Direct (malloc-backed) buffers, matching libGDX/gdx-teavm. Buffers.addr
     * gets their stable native address via TeaVM's TNativeBufferUtil, so the
     * memory survives GC between glVertexAttribPointer and the draw call.
     * Native byte order so asXBuffer() views are native-endian for GL. */

    public static ByteBuffer newByteBuffer(int numBytes) {
        return ByteBuffer.allocate(numBytes).order(ByteOrder.nativeOrder());
    }

    public static FloatBuffer newFloatBuffer(int numFloats) {
        return newByteBuffer(numFloats * 4).asFloatBuffer();
    }

    public static DoubleBuffer newDoubleBuffer(int numDoubles) {
        return newByteBuffer(numDoubles * 8).asDoubleBuffer();
    }

    public static ShortBuffer newShortBuffer(int numShorts) {
        return newByteBuffer(numShorts * 2).asShortBuffer();
    }

    public static CharBuffer newCharBuffer(int numChars) {
        return newByteBuffer(numChars * 2).asCharBuffer();
    }

    public static IntBuffer newIntBuffer(int numInts) {
        return newByteBuffer(numInts * 4).asIntBuffer();
    }

    public static LongBuffer newLongBuffer(int numLongs) {
        return newByteBuffer(numLongs * 8).asLongBuffer();
    }

    public static ByteBuffer newUnsafeByteBuffer(int numBytes) {
        ByteBuffer buffer = newByteBuffer(numBytes);
        allocatedUnsafe += numBytes;
        synchronized (unsafeBuffers) {
            unsafeBuffers.add(buffer);
        }
        return buffer;
    }

    public static ByteBuffer newUnsafeByteBuffer(ByteBuffer buffer) {
        allocatedUnsafe += buffer.capacity();
        synchronized (unsafeBuffers) {
            unsafeBuffers.add(buffer);
        }
        return buffer;
    }

    public static void disposeUnsafeByteBuffer(ByteBuffer buffer) {
        int size = buffer.capacity();
        synchronized (unsafeBuffers) {
            boolean removed = false;
            for (int i = 0; i < unsafeBuffers.size(); i++) {
                if (unsafeBuffers.get(i) == buffer) {
                    unsafeBuffers.remove(i);
                    removed = true;
                    break;
                }
            }
            if (!removed) {
                throw new IllegalArgumentException(
                        "buffer not allocated with newUnsafeByteBuffer or already disposed");
            }
        }
        allocatedUnsafe -= size;
        // Memory itself is GC-managed (TeaVM registers direct buffers).
    }

    public static int getAllocatedBytesUnsafe() {
        return allocatedUnsafe;
    }

    public static long getUnsafeBufferAddress(Buffer buffer) {
        return Buffers.addr(buffer).toLong();
    }

    public static void clear(ByteBuffer buffer, int numBytes) {
        memset(Buffers.addr(buffer, 0), 0, numBytes);
    }

    /* ---- copies ----
     * When dst has a backing array we memcpy straight into it (fast, and
     * writes through). When dst is a typed VIEW over an array-backed byte
     * buffer (no array()), memcpy to Buffers.addr would hit a throwaway copy,
     * so we write through the nio typed put API instead (reaches the shared
     * backing). Position is saved/restored to match the JNI's non-advancing
     * semantics; limit updates mirror libGDX. */

    public static void copy(float[] src, Buffer dst, int numFloats, int offset) {
        dst.position(0);
        if (dst.hasArray()) {
            memcpy(Buffers.addr(dst), Address.ofData(src).add(offset << 2), numFloats << 2);
        } else {
            FloatBuffer fb = (FloatBuffer) dst;
            fb.position(0);
            fb.put(src, offset, numFloats);
            fb.position(0);
        }
        dst.position(0);
        dst.limit(dst instanceof ByteBuffer ? numFloats << 2 : numFloats);
    }

    public static void copy(byte[] src, int srcOffset, Buffer dst, int numElements) {
        dst.limit(dst.position() + bytesToElements(dst, numElements));
        // byte[] dst is always a ByteBuffer (array-backed) in practice
        memcpy(Buffers.addr(dst), Address.ofData(src).add(srcOffset), numElements);
    }

    public static void copy(short[] src, int srcOffset, Buffer dst, int numElements) {
        dst.limit(dst.position() + bytesToElements(dst, numElements << 1));
        putShorts(dst, src, srcOffset, numElements);
    }

    public static void copy(char[] src, int srcOffset, int numElements, Buffer dst) {
        putChars(dst, src, srcOffset, numElements);
    }

    public static void copy(int[] src, int srcOffset, int numElements, Buffer dst) {
        putInts(dst, src, srcOffset, numElements);
    }

    public static void copy(long[] src, int srcOffset, int numElements, Buffer dst) {
        memcpy(Buffers.addr(dst), Address.ofData(src).add(srcOffset << 3), numElements << 3);
    }

    public static void copy(float[] src, int srcOffset, int numElements, Buffer dst) {
        putFloats(dst, src, srcOffset, numElements);
    }

    public static void copy(double[] src, int srcOffset, int numElements, Buffer dst) {
        memcpy(Buffers.addr(dst), Address.ofData(src).add(srcOffset << 3), numElements << 3);
    }

    public static void copy(char[] src, int srcOffset, Buffer dst, int numElements) {
        dst.limit(dst.position() + bytesToElements(dst, numElements << 1));
        putChars(dst, src, srcOffset, numElements);
    }

    public static void copy(int[] src, int srcOffset, Buffer dst, int numElements) {
        dst.limit(dst.position() + bytesToElements(dst, numElements << 2));
        putInts(dst, src, srcOffset, numElements);
    }

    public static void copy(long[] src, int srcOffset, Buffer dst, int numElements) {
        dst.limit(dst.position() + bytesToElements(dst, numElements << 3));
        memcpy(Buffers.addr(dst), Address.ofData(src).add(srcOffset << 3), numElements << 3);
    }

    public static void copy(float[] src, int srcOffset, Buffer dst, int numElements) {
        dst.limit(dst.position() + bytesToElements(dst, numElements << 2));
        putFloats(dst, src, srcOffset, numElements);
    }

    public static void copy(double[] src, int srcOffset, Buffer dst, int numElements) {
        dst.limit(dst.position() + bytesToElements(dst, numElements << 3));
        memcpy(Buffers.addr(dst), Address.ofData(src).add(srcOffset << 3), numElements << 3);
    }

    public static void copy(Buffer src, Buffer dst, int numElements) {
        int numBytes = elementsToBytes(src, numElements);
        dst.limit(dst.position() + bytesToElements(dst, numBytes));
        memcpy(Buffers.addr(dst), Buffers.addr(src), numBytes);
    }

    // Write src[srcOff..srcOff+n] at dst's current position without advancing
    // it. Fast path (hasArray) memcpys; view path writes through the nio API.

    private static void putFloats(Buffer dst, float[] src, int srcOff, int n) {
        if (dst.hasArray()) {
            memcpy(Buffers.addr(dst), Address.ofData(src).add(srcOff << 2), n << 2);
            return;
        }
        FloatBuffer fb = (FloatBuffer) dst;
        int p = fb.position();
        fb.put(src, srcOff, n);
        fb.position(p);
    }

    private static void putShorts(Buffer dst, short[] src, int srcOff, int n) {
        if (dst.hasArray()) {
            memcpy(Buffers.addr(dst), Address.ofData(src).add(srcOff << 1), n << 1);
            return;
        }
        ShortBuffer sb = (ShortBuffer) dst;
        int p = sb.position();
        sb.put(src, srcOff, n);
        sb.position(p);
    }

    private static void putInts(Buffer dst, int[] src, int srcOff, int n) {
        if (dst.hasArray()) {
            memcpy(Buffers.addr(dst), Address.ofData(src).add(srcOff << 2), n << 2);
            return;
        }
        IntBuffer ib = (IntBuffer) dst;
        int p = ib.position();
        ib.put(src, srcOff, n);
        ib.position(p);
    }

    private static void putChars(Buffer dst, char[] src, int srcOff, int n) {
        if (dst.hasArray()) {
            memcpy(Buffers.addr(dst), Address.ofData(src).add(srcOff << 1), n << 1);
            return;
        }
        CharBuffer cb = (CharBuffer) dst;
        int p = cb.position();
        cb.put(src, srcOff, n);
        cb.position(p);
    }

    /* ---- transforms (pure Java; cold paths in this game) ---- */

    public static void transform(Buffer data, int dimensions, int strideInBytes, int count, Matrix4 matrix) {
        transform(data, dimensions, strideInBytes, count, matrix, 0);
    }

    public static void transform(float[] data, int dimensions, int strideInBytes, int count, Matrix4 matrix) {
        transform(data, dimensions, strideInBytes, count, matrix, 0);
    }

    public static void transform(Buffer data, int dimensions, int strideInBytes, int count, Matrix4 matrix, int offset) {
        FloatBuffer f = asFloatView(data);
        int strideF = strideInBytes / 4;
        int start = f.position() + offset / 4;
        for (int i = 0; i < count; i++) {
            transformOne(f, start + i * strideF, dimensions, matrix.val);
        }
    }

    public static void transform(float[] data, int dimensions, int strideInBytes, int count, Matrix4 matrix, int offset) {
        int strideF = strideInBytes / 4;
        int start = offset / 4;
        for (int i = 0; i < count; i++) {
            transformOneArray(data, start + i * strideF, dimensions, matrix.val);
        }
    }

    public static void transform(Buffer data, int dimensions, int strideInBytes, int count, Matrix3 matrix) {
        transform(data, dimensions, strideInBytes, count, matrix, 0);
    }

    public static void transform(float[] data, int dimensions, int strideInBytes, int count, Matrix3 matrix) {
        transform(data, dimensions, strideInBytes, count, matrix, 0);
    }

    public static void transform(Buffer data, int dimensions, int strideInBytes, int count, Matrix3 matrix, int offset) {
        FloatBuffer f = asFloatView(data);
        int strideF = strideInBytes / 4;
        int start = f.position() + offset / 4;
        for (int i = 0; i < count; i++) {
            transformOne3(f, start + i * strideF, dimensions, matrix.val);
        }
    }

    public static void transform(float[] data, int dimensions, int strideInBytes, int count, Matrix3 matrix, int offset) {
        int strideF = strideInBytes / 4;
        int start = offset / 4;
        for (int i = 0; i < count; i++) {
            transformOne3Array(data, start + i * strideF, dimensions, matrix.val);
        }
    }

    private static void transformOne(FloatBuffer f, int p, int dim, float[] m) {
        float x = f.get(p);
        float y = dim >= 2 ? f.get(p + 1) : 0;
        float z = dim >= 3 ? f.get(p + 2) : 0;
        float w = dim == 4 ? f.get(p + 3) : 1;
        f.put(p, x * m[0] + y * m[4] + z * m[8] + w * m[12]);
        if (dim >= 2) f.put(p + 1, x * m[1] + y * m[5] + z * m[9] + w * m[13]);
        if (dim >= 3) f.put(p + 2, x * m[2] + y * m[6] + z * m[10] + w * m[14]);
        if (dim == 4) f.put(p + 3, x * m[3] + y * m[7] + z * m[11] + w * m[15]);
    }

    private static void transformOneArray(float[] f, int p, int dim, float[] m) {
        float x = f[p];
        float y = dim >= 2 ? f[p + 1] : 0;
        float z = dim >= 3 ? f[p + 2] : 0;
        float w = dim == 4 ? f[p + 3] : 1;
        f[p] = x * m[0] + y * m[4] + z * m[8] + w * m[12];
        if (dim >= 2) f[p + 1] = x * m[1] + y * m[5] + z * m[9] + w * m[13];
        if (dim >= 3) f[p + 2] = x * m[2] + y * m[6] + z * m[10] + w * m[14];
        if (dim == 4) f[p + 3] = x * m[3] + y * m[7] + z * m[11] + w * m[15];
    }

    private static void transformOne3(FloatBuffer f, int p, int dim, float[] m) {
        float x = f.get(p);
        float y = dim >= 2 ? f.get(p + 1) : 0;
        float z = dim == 3 ? f.get(p + 2) : 1;
        f.put(p, x * m[0] + y * m[3] + z * m[6]);
        if (dim >= 2) f.put(p + 1, x * m[1] + y * m[4] + z * m[7]);
        if (dim == 3) f.put(p + 2, x * m[2] + y * m[5] + z * m[8]);
    }

    private static void transformOne3Array(float[] f, int p, int dim, float[] m) {
        float x = f[p];
        float y = dim >= 2 ? f[p + 1] : 0;
        float z = dim == 3 ? f[p + 2] : 1;
        f[p] = x * m[0] + y * m[3] + z * m[6];
        if (dim >= 2) f[p + 1] = x * m[1] + y * m[4] + z * m[7];
        if (dim == 3) f[p + 2] = x * m[2] + y * m[5] + z * m[8];
    }

    /* ---- findFloats (vertex dedup; cold) ---- */

    public static long findFloats(Buffer vertex, int strideInBytes, Buffer vertices, int numVertices) {
        return find(floats(vertex, strideInBytes / 4), floats(vertices, strideInBytes / 4 * numVertices),
                strideInBytes / 4, numVertices, 0f, false);
    }

    public static long findFloats(float[] vertex, int strideInBytes, Buffer vertices, int numVertices) {
        return find(vertex, floats(vertices, strideInBytes / 4 * numVertices),
                strideInBytes / 4, numVertices, 0f, false);
    }

    public static long findFloats(Buffer vertex, int strideInBytes, float[] vertices, int numVertices) {
        return find(floats(vertex, strideInBytes / 4), vertices, strideInBytes / 4, numVertices, 0f, false);
    }

    public static long findFloats(float[] vertex, int strideInBytes, float[] vertices, int numVertices) {
        return find(vertex, vertices, strideInBytes / 4, numVertices, 0f, false);
    }

    public static long findFloats(Buffer vertex, int strideInBytes, Buffer vertices, int numVertices, float epsilon) {
        return find(floats(vertex, strideInBytes / 4), floats(vertices, strideInBytes / 4 * numVertices),
                strideInBytes / 4, numVertices, epsilon, true);
    }

    public static long findFloats(float[] vertex, int strideInBytes, Buffer vertices, int numVertices, float epsilon) {
        return find(vertex, floats(vertices, strideInBytes / 4 * numVertices),
                strideInBytes / 4, numVertices, epsilon, true);
    }

    public static long findFloats(Buffer vertex, int strideInBytes, float[] vertices, int numVertices, float epsilon) {
        return find(floats(vertex, strideInBytes / 4), vertices, strideInBytes / 4, numVertices, epsilon, true);
    }

    public static long findFloats(float[] vertex, int strideInBytes, float[] vertices, int numVertices, float epsilon) {
        return find(vertex, vertices, strideInBytes / 4, numVertices, epsilon, true);
    }

    private static float[] floats(Buffer buffer, int count) {
        FloatBuffer f = asFloatView(buffer);
        float[] out = new float[count];
        int p = f.position();
        for (int i = 0; i < count; i++) {
            out[i] = f.get(p + i);
        }
        return out;
    }

    private static long find(float[] vertex, float[] vertices, int strideF, int numVertices,
                             float epsilon, boolean useEpsilon) {
        for (int v = 0; v < numVertices; v++) {
            boolean equal = true;
            for (int i = 0; i < strideF; i++) {
                float diff = vertices[v * strideF + i] - vertex[i];
                if (useEpsilon ? Math.abs(diff) > epsilon : diff != 0) {
                    equal = false;
                    break;
                }
            }
            if (equal) {
                return v;
            }
        }
        return -1;
    }

    /* ---- helpers ---- */

    private static Address base(Buffer buffer) {
        // buffer base (views embed their creation position in the address)
        return Buffers.addr(buffer, 0).add(-positionInBytes0(buffer));
    }

    private static int positionInBytes0(Buffer dst) {
        return 0;
    }

    private static int positionInBytes(Buffer dst) {
        if (dst instanceof ByteBuffer) {
            return dst.position();
        } else if (dst instanceof ShortBuffer || dst instanceof CharBuffer) {
            return dst.position() << 1;
        } else if (dst instanceof IntBuffer || dst instanceof FloatBuffer) {
            return dst.position() << 2;
        } else if (dst instanceof LongBuffer || dst instanceof DoubleBuffer) {
            return dst.position() << 3;
        }
        throw new GdxRuntimeException("Can't copy to a " + dst.getClass().getName());
    }

    private static int bytesToElements(Buffer dst, int bytes) {
        if (dst instanceof ByteBuffer) {
            return bytes;
        } else if (dst instanceof ShortBuffer || dst instanceof CharBuffer) {
            return bytes >>> 1;
        } else if (dst instanceof IntBuffer || dst instanceof FloatBuffer) {
            return bytes >>> 2;
        } else if (dst instanceof LongBuffer || dst instanceof DoubleBuffer) {
            return bytes >>> 3;
        }
        throw new GdxRuntimeException("Can't copy to a " + dst.getClass().getName());
    }

    private static int elementsToBytes(Buffer dst, int elements) {
        if (dst instanceof ByteBuffer) {
            return elements;
        } else if (dst instanceof ShortBuffer || dst instanceof CharBuffer) {
            return elements << 1;
        } else if (dst instanceof IntBuffer || dst instanceof FloatBuffer) {
            return elements << 2;
        } else if (dst instanceof LongBuffer || dst instanceof DoubleBuffer) {
            return elements << 3;
        }
        throw new GdxRuntimeException("Can't copy to a " + dst.getClass().getName());
    }

    private static FloatBuffer asFloatView(Buffer data) {
        if (data instanceof FloatBuffer) {
            return (FloatBuffer) data;
        }
        if (data instanceof ByteBuffer) {
            return ((ByteBuffer) data).asFloatBuffer();
        }
        throw new GdxRuntimeException("Can't transform a " + data.getClass().getName());
    }

    @Import(name = "stsv_memcpy")
    private static native void memcpy(Address dst, Address src, int bytes);

    @Import(name = "stsv_memset")
    private static native void memset(Address dst, int value, int bytes);

    private BufferUtils() {}
}
