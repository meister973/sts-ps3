package com.stsvita.backend;

import java.util.HashMap;
import java.util.Map;

import com.megacrit.cardcrawl.rooms.EmptyRoom;
import com.megacrit.cardcrawl.rooms.EventRoom;
import com.megacrit.cardcrawl.rooms.MonsterRoom;
import com.megacrit.cardcrawl.rooms.MonsterRoomBoss;
import com.megacrit.cardcrawl.rooms.MonsterRoomElite;
import com.megacrit.cardcrawl.rooms.RestRoom;
import com.megacrit.cardcrawl.rooms.ShopRoom;
import com.megacrit.cardcrawl.rooms.TreasureRoom;
import com.megacrit.cardcrawl.rooms.TreasureRoomBoss;
import com.megacrit.cardcrawl.rooms.TrueVictoryRoom;

/**
 * Constructor-reflection replacement for {@code Class.newInstance()}, which the
 * TeaVM C backend cannot codegen (no constructor-invocation intrinsic). The
 * only {@code Class.newInstance()} call site kept at runtime is
 * {@code RoomTypeAssigner.assignRowAsRoomType}, which instantiates the map's
 * {@link com.megacrit.cardcrawl.rooms.AbstractRoom} types from a
 * {@code Class} constant. {@code ClasslibShims} rewrites
 * {@code java.lang.Class.newInstance()} to call {@link #create(Class)} instead
 * of throwing; each type is created here by a direct no-arg constructor (no
 * reflection). A missing type throws a clear error naming the class to add.
 */
public final class NewInstanceRegistry {

    private interface Factory {
        Object create();
    }

    private static final Map<String, Factory> FACTORIES = new HashMap<>();

    static {
        FACTORIES.put("com.megacrit.cardcrawl.rooms.RestRoom", RestRoom::new);
        FACTORIES.put("com.megacrit.cardcrawl.rooms.MonsterRoom", MonsterRoom::new);
        FACTORIES.put("com.megacrit.cardcrawl.rooms.MonsterRoomElite", MonsterRoomElite::new);
        FACTORIES.put("com.megacrit.cardcrawl.rooms.MonsterRoomBoss", MonsterRoomBoss::new);
        FACTORIES.put("com.megacrit.cardcrawl.rooms.EventRoom", EventRoom::new);
        FACTORIES.put("com.megacrit.cardcrawl.rooms.ShopRoom", ShopRoom::new);
        FACTORIES.put("com.megacrit.cardcrawl.rooms.TreasureRoom", TreasureRoom::new);
        FACTORIES.put("com.megacrit.cardcrawl.rooms.TreasureRoomBoss", TreasureRoomBoss::new);
        FACTORIES.put("com.megacrit.cardcrawl.rooms.TrueVictoryRoom", TrueVictoryRoom::new);
        FACTORIES.put("com.megacrit.cardcrawl.rooms.EmptyRoom", EmptyRoom::new);
    }

    private NewInstanceRegistry() {
    }

    /** Body that {@code ClasslibShims} substitutes for {@code Class.newInstance()}. */
    public static Object create(Class<?> type) {
        Factory factory = type == null ? null : FACTORIES.get(type.getName());
        if (factory == null) {
            throw new UnsupportedOperationException(
                    "STSVITA: no newInstance factory for "
                    + (type == null ? "null" : type.getName())
                    + " (add it to NewInstanceRegistry)");
        }
        return factory.create();
    }
}
