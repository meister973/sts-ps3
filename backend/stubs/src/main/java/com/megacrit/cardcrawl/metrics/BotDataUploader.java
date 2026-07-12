package com.megacrit.cardcrawl.metrics;

import java.io.Serializable;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * Shadow of the game's telemetry uploader (second of the two game-package
 * classes we replace; see AsyncSaver for the policy). Justification: pure
 * metrics/HTTP plumbing with zero game logic, and its java.net usage is
 * outside TeaVM's classlib. Equivalent to playing desktop offline.
 * Public surface matches the original.
 */
public class BotDataUploader implements Runnable {

    public enum GameDataType {
        BANDITS,
        CRASH_DATA,
        DAILY_DATA,
        DEMO_EMBARK,
        VICTORY_DATA,
        CARD_DATA,
        ENEMY_DATA,
        RELIC_DATA,
        POTION_DATA,
        DAILY_MOD_DATA,
        BLIGHT_DATA,
        KEYWORD_DATA
    }

    public void setValues(GameDataType type, String header, ArrayList<String> data) {
    }

    public void sendPost(HashMap<String, Serializable> eventData) {
    }

    public static void uploadDataAsync(GameDataType type, String header, ArrayList<String> data) {
    }

    public static void uploadKeywordData() {
    }

    @Override
    public void run() {
    }
}
