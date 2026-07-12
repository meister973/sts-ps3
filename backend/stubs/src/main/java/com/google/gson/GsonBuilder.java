package com.google.gson;

public class GsonBuilder {

    private boolean pretty;

    public GsonBuilder setPrettyPrinting() {
        pretty = true;
        return this;
    }

    public Gson create() {
        return new Gson(pretty);
    }
}
