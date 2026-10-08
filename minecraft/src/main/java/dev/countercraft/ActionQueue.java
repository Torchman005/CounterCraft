package dev.countercraft;

import com.google.gson.*;
import java.util.ArrayDeque;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.function.Function;

/** Network requests are immutable; only tick's executor may call game APIs. */
public final class ActionQueue {
    public static final int CAPACITY = 32;
    public static final long TTL_NS = 400_000_000L, MIN_INTERVAL_NS = 20_000_000L;
    public record Action(long id, long epoch, String kind, double x, double y, double z,
                         float yaw, float pitch, String face, int slot, int button, String item, int count) {}
    private record Pending(Action action, long time, CompletableFuture<JsonObject> result) {}
    private final ArrayDeque<Pending> pending = new ArrayDeque<>();
    private long lastId = -1, lastTime = Long.MIN_VALUE;

    public static Action parse(JsonObject m) {
        long id = integer(m, "id"), epoch = integer(m, "epoch");
        String kind = m.get("action").getAsString();
        if (id < 0 || epoch < 0 || !Set.of("look", "break", "place", "select", "inventory", "click", "inspect", "move", "creative", "target", "ui", "entities").contains(kind))
            throw new IllegalArgumentException("Unknown action or negative id/epoch");
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
        String face = "up";
        int slot = 0, button = 0;
        String item = ""; int count = 0;
        if (Set.of("break", "place", "inspect", "move").contains(kind)) {
            JsonArray p = m.getAsJsonArray(kind.equals("move") ? "delta" : "block");
            if (p == null || p.size() != 3) throw new IllegalArgumentException("Expected three coordinates");
            x = number(p.get(0)); y = number(p.get(1)); z = number(p.get(2));
            if (kind.equals("move")) {
                if (x*x+y*y+z*z > 4) throw new IllegalArgumentException("Movement step exceeds two blocks");
            } else if (Math.abs(x) > 29_999_984 || Math.abs(z) > 29_999_984 || y < -2048 || y > 2047
                    || x != Math.rint(x) || y != Math.rint(y) || z != Math.rint(z))
                throw new IllegalArgumentException("Block coordinate outside bounds");
        }
        if (kind.equals("look")) {
            yaw = (float) number(m.get("yaw")); pitch = (float) number(m.get("pitch"));
            if (!Float.isFinite(yaw) || Math.abs(pitch) > 90) throw new IllegalArgumentException("Invalid look angles");
        }
        if (kind.equals("break") || kind.equals("place")) {
            face = m.get("face").getAsString();
            if (!Set.of("up", "down", "north", "south", "east", "west").contains(face))
                throw new IllegalArgumentException("Invalid block face");
        }
        if (kind.equals("select") || kind.equals("click") || kind.equals("creative")) {
            long value = integer(m, "slot");
            if (value < 0 || value > (kind.equals("click") ? 45 : 8)) throw new IllegalArgumentException("Invalid inventory slot");
            slot = (int) value;
        }
        if (kind.equals("click")) {
            long value = integer(m, "button");
            if (value < 0 || value > 1) throw new IllegalArgumentException("Button must be 0 or 1");
            button = (int) value;
        }
        if (kind.equals("creative")) {
            item = m.get("item").getAsString();
            if (item.length() > 128 || !item.matches("[a-z0-9_.-]+:[a-z0-9_./-]+")) throw new IllegalArgumentException("Invalid item id");
            long value = integer(m, "count");
            if (value < 1 || value > 64) throw new IllegalArgumentException("Invalid item count");
            count = (int) value;
        }
        if (kind.equals("ui")) {
            if(m.has("text")) {
                var value=m.get("text");
                if(!value.isJsonPrimitive() || !value.getAsJsonPrimitive().isString()) throw new IllegalArgumentException("Expected UI string");
                item=value.getAsString();
                if(item.length()>64 || item.codePoints().anyMatch(c -> c<32 || c==127 || (c>=0xD800 && c<=0xDFFF)))
                    throw new IllegalArgumentException("UI text outside bounds");
            }
            long key=m.has("key")?integer(m,"key"):0, modifiers=m.has("modifiers")?integer(m,"modifiers"):0;
            if(!Set.of(0L,47L,84L,256L,257L,258L,259L,261L,262L,263L,264L,265L,268L,269L).contains(key)
                || modifiers<0 || modifiers>7 || (key!=0 && !item.isEmpty()) || (key==0 && item.isEmpty()))
                throw new IllegalArgumentException("Invalid UI key/modifiers or empty event");
            button=(int)key;slot=(int)modifiers;
        }
        return new Action(id, epoch, kind, x, y, z, yaw, pitch, face, slot, button, item, count);
    }
    private static double number(JsonElement e) {
        if (e == null || !e.isJsonPrimitive() || !e.getAsJsonPrimitive().isNumber() || !Double.isFinite(e.getAsDouble()))
            throw new IllegalArgumentException("Expected finite number");
        return e.getAsDouble();
    }
    private static long integer(JsonObject m, String key) { number(m.get(key)); return m.get(key).getAsBigDecimal().longValueExact(); }

    public synchronized CompletableFuture<JsonObject> submit(Action a, BridgeState.World w, long now) {
        if (!w.offline() || now - w.time() > BridgeState.TIMEOUT_NS || a.epoch != w.epoch())
            throw new IllegalArgumentException("Action requires current unpaused singleplayer epoch");
        if (a.id <= lastId) throw new IllegalArgumentException("Action id must increase");
        if (lastTime != Long.MIN_VALUE && now - lastTime < MIN_INTERVAL_NS)
            throw new IllegalArgumentException("Action rate exceeds 50 requests per second");
        if (pending.size() >= CAPACITY) throw new IllegalArgumentException("Action queue full");
        var result = new CompletableFuture<JsonObject>();
        pending.add(new Pending(a, now, result)); lastId = a.id; lastTime = now;
        return result;
    }
    public synchronized void tick(BridgeState.World w, long now, Function<Action, JsonObject> executor) {
        // Holding this short lock makes disconnect/clear linearize with an action.
        for (int n = 0; n < 8 && !pending.isEmpty(); n++) {
            Pending p = pending.remove();
            if (p.result.isDone()) continue;
            if (!w.offline() || p.action.epoch != w.epoch() || now-p.time > TTL_NS || now-w.time() > BridgeState.TIMEOUT_NS) {
                p.result.completeExceptionally(new IllegalArgumentException("Action expired or world unavailable")); continue;
            }
            try {
                JsonObject reply = executor.apply(p.action);
                reply.addProperty("type", "action-ack"); reply.addProperty("id", p.action.id);
                reply.addProperty("epoch", w.epoch()); reply.addProperty("action", p.action.kind);
                p.result.complete(reply);
            } catch (RuntimeException e) { p.result.completeExceptionally(e); }
        }
    }
    public synchronized void clear(String reason) {
        while (!pending.isEmpty()) pending.remove().result.completeExceptionally(new IllegalArgumentException(reason));
    }
    public synchronized void reset() { clear("Host disconnected"); lastId = -1; lastTime = Long.MIN_VALUE; }
}
