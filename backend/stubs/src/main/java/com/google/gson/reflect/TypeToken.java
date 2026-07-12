package com.google.gson.reflect;

import java.lang.reflect.Type;
import java.util.HashMap;
import java.util.Map;

/**
 * gson TypeToken shadow. Real gson resolves the captured type argument via
 * Class.getGenericSuperclass(), which TeaVM's classlib doesn't provide.
 * Instead, the asset pipeline scans the user's jar for TypeToken subclasses
 * at build time and registers each one's generic signature here (the game
 * only ever creates them as anonymous subclasses, e.g. SaveHelper$1), so
 * resolution becomes a name lookup with no runtime generic reflection.
 *
 * Until the generated manifest lands, unregistered tokens resolve to their
 * raw marker and Gson's binder treats them as Map/List of JSON natives.
 */
public class TypeToken<T> implements Type {

    private static final Map<String, Type> MANIFEST = new HashMap<>();

    protected TypeToken() {
    }

    /** Called by the build-time-generated TypeManifest class. */
    public static void stsvRegister(String subclassName, Type resolved) {
        MANIFEST.put(subclassName, resolved);
    }

    public Type getType() {
        Type resolved = MANIFEST.get(getClass().getName());
        return resolved != null ? resolved : this;
    }
}
