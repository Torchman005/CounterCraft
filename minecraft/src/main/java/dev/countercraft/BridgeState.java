package dev.countercraft;

/** Immutable snapshots cross threads; networking never calls Minecraft APIs. */
public final class BridgeState {
    public static final long TIMEOUT_NS = 500_000_000L;
    public record World(long epoch, boolean offline, double x, double y, double z, long time) {}
    public record Pose(long epoch, long frame, double x, double y, double z,
                       float yaw, float pitch, float fov, long time) {}
    private volatile World world = new World(0, false, 0, 0, 0, 0);
    private volatile Pose pose;

    public World world() { return world; }
    public void update(World next) { world = next; }
    public void release() { pose = null; }

    public synchronized void accept(long frame, double x, double y, double z,
                                    float yaw, float pitch, float fov, long now) {
        World current = world;
        if (!current.offline || now - current.time > TIMEOUT_NS)
            throw new IllegalArgumentException("An active unpaused single-player world is required");
        if (frame < 0 || !Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)
                || !Float.isFinite(yaw) || !Float.isFinite(pitch) || !Float.isFinite(fov)
                || Math.abs(pitch) > 90 || fov <= 0 || fov >= 180)
            throw new IllegalArgumentException("Invalid camera values");
        double dx = x - current.x, dy = y - current.y, dz = z - current.z;
        if (dx * dx + dy * dy + dz * dz > 64 * 64)
            throw new IllegalArgumentException("Camera must stay within 64 blocks of the player");
        pose = new Pose(current.epoch, frame, x, y, z, yaw, pitch, fov, now);
    }

    public Pose live(long now) {
        World current = world;
        Pose latest = pose;
        return latest != null && current.offline && latest.epoch == current.epoch
                && now - latest.time <= TIMEOUT_NS && now - current.time <= TIMEOUT_NS
                ? latest : null;
    }
}
