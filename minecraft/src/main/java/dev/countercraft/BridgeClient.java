package dev.countercraft;

import net.fabricmc.api.ClientModInitializer;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.util.math.Vec3d;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public final class BridgeClient implements ClientModInitializer {
    public static final BridgeState STATE = new BridgeState();
    private static final Logger LOG = LoggerFactory.getLogger("CounterCraft");
    private static ClientWorld previous;
    private static long epoch;
    private static boolean enabled;
    private static volatile BridgeState.Pose framePose;

    @Override public void onInitializeClient() {
        if (!Boolean.getBoolean("countercraft.enabled")) {
            LOG.info("Camera lab disabled. Opt in with -Dcountercraft.enabled=true");
            return;
        }
        try {
            HostServer server = new HostServer(STATE, Integer.getInteger("countercraft.port", 37122));
            enabled = true;
            Runtime.getRuntime().addShutdownHook(new Thread(() -> {
                try { server.close(); } catch (Exception ignored) { }
            }, "CounterCraft-Shutdown"));
            LOG.info("Camera lab listening on 127.0.0.1:{}; single-player only", server.port());
        } catch (Exception e) {
            LOG.error("Cannot start camera lab; vanilla camera retained", e);
        }
    }

    public static void tick(MinecraftClient client) {
        if (!enabled) return;
        if (previous != client.world) { previous = client.world; epoch++; STATE.release(); }
        boolean offline = client.world != null && client.player != null
                && client.isInSingleplayer() && !client.isPaused();
        Vec3d pos = client.player == null ? Vec3d.ZERO : client.player.getEyePos();
        STATE.update(new BridgeState.World(epoch, offline, pos.x, pos.y, pos.z, System.nanoTime()));
        if (!offline) STATE.release();
    }

    public static void beginFrame() {
        MinecraftClient client = MinecraftClient.getInstance();
        framePose = enabled && client.world != null && client.world == previous
                && client.isInSingleplayer() && !client.isPaused() ? STATE.live(System.nanoTime()) : null;
    }
    public static BridgeState.Pose framePose() { return framePose; }
}
