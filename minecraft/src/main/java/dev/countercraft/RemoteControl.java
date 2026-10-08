package dev.countercraft;

import com.google.gson.*;

/** Latest immutable held-input state; game APIs consume it only on client ticks. */
public final class RemoteControl {
    public static final long TIMEOUT_NS = 250_000_000L;
    public record Input(long id, long epoch, float yaw, float pitch, float forward, float sideways,
                        boolean jump, boolean sneak, boolean sprint, boolean attack, boolean use,
                        boolean inventory, boolean escape, boolean drop, boolean swap, boolean pick,
                        int slot, double mouseX, double mouseY, long scroll, long time) {}
    private volatile Input input;
    private volatile JsonObject player = new JsonObject();
    private long last = -1;
    private static double number(JsonElement value) {
        if (value == null || !value.isJsonPrimitive() || !value.getAsJsonPrimitive().isNumber()
                || !Double.isFinite(value.getAsDouble())) throw new IllegalArgumentException("Expected finite input number");
        return value.getAsDouble();
    }
    private static long integer(JsonObject m, String key) {
        number(m.get(key));
        try { return m.get(key).getAsBigDecimal().longValueExact(); }
        catch (ArithmeticException invalid) { throw new IllegalArgumentException("Expected integer input"); }
    }
    private static boolean flag(JsonObject m, String key) {
        if (!m.has(key)) return false;
        var p = m.get(key);
        if (!p.isJsonPrimitive() || !p.getAsJsonPrimitive().isBoolean()) throw new IllegalArgumentException("Expected boolean input");
        return p.getAsBoolean();
    }
    public synchronized Input accept(JsonObject m, BridgeState.World world, long now) {
        long id = integer(m, "id"), epoch = integer(m, "epoch");
        if (id <= last || id < 0 || epoch != world.epoch() || !world.offline() || now-world.time()>BridgeState.TIMEOUT_NS)
            throw new IllegalArgumentException("Input requires increasing id and current offline epoch");
        if (input != null && now-input.time < 10_000_000) throw new IllegalArgumentException("Input exceeds 100 Hz");
        float yaw = (float) number(m.get("yaw")), pitch = (float) number(m.get("pitch"));
        float forward = (float) number(m.get("forward")), sideways = (float) number(m.get("sideways"));
        long slot = integer(m, "slot");
        long scroll = m.has("scroll") ? integer(m, "scroll") : 0;
        double mx = m.has("mouseX") ? number(m.get("mouseX")) : .5, my = m.has("mouseY") ? number(m.get("mouseY")) : .5;
        if (!Float.isFinite(yaw) || Math.abs(pitch)>90 || Math.abs(forward)>1 || Math.abs(sideways)>1
                || slot < 0 || slot > 8 || scroll < -1_000_000 || scroll > 1_000_000 || mx<0 || mx>1 || my<0 || my>1) throw new IllegalArgumentException("Input outside bounds");
        Input next = new Input(id, epoch, yaw, pitch, forward, sideways, flag(m,"jump"), flag(m,"sneak"), flag(m,"sprint"),
                flag(m,"attack"), flag(m,"use"), flag(m,"inventory"), flag(m,"escape"), flag(m,"drop"), flag(m,"swap"), flag(m,"pick"), (int) slot, mx, my, scroll, now);
        input = next; last = id; return next;
    }
    public Input live(BridgeState.World world, long now) {
        Input value = input;
        return value != null && world.offline() && value.epoch == world.epoch() && now-value.time <= TIMEOUT_NS
                && now-world.time()<=BridgeState.TIMEOUT_NS ? value : null;
    }
    public synchronized void release() { input = null; }
    public synchronized void reset() { input = null; last = -1; }
    public void publish(JsonObject snapshot) { player = snapshot.deepCopy(); }
    public JsonObject player() { return player.deepCopy(); }
}
