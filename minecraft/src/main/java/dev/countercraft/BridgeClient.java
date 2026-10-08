package dev.countercraft;

import net.fabricmc.api.ClientModInitializer;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.util.math.Vec3d;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import net.minecraft.client.render.Camera;
import net.minecraft.client.util.math.MatrixStack;
import com.google.gson.*;
import dev.countercraft.mixin.InteractionInvoker;
import net.minecraft.client.gui.screen.ingame.HandledScreen;
import net.minecraft.client.gui.screen.ingame.InventoryScreen;

public final class BridgeClient implements ClientModInitializer {
    public static final BridgeState STATE = new BridgeState();
    private static final Logger LOG = LoggerFactory.getLogger("CounterCraft");
    private static ClientWorld previous;
    private static long epoch;
    private static boolean enabled;
    private static volatile BridgeState.Pose framePose;
    private static FrameCapture captures;
    private static FrameStream frames;
    private static final ActionQueue actions = new ActionQueue();
    private static final RemoteControl controls = new RemoteControl();
    private static RemoteControl.Input previousInput;
    private static int attackTicks, useTicks;

    @Override public void onInitializeClient() {
        if (!Boolean.getBoolean("countercraft.enabled")) {
            LOG.info("Camera lab disabled. Opt in with -Dcountercraft.enabled=true");
            return;
        }
        try {
            captures = new FrameCapture(clientCaptureRoot());
            frames = new FrameStream();
            HostServer server = new HostServer(STATE, Integer.getInteger("countercraft.port", 37122), captures::request, frames, actions, controls);
            enabled = true;
            Runtime.getRuntime().addShutdownHook(new Thread(() -> {
                try { server.close(); } catch (Exception ignored) { }
                captures.close();
                frames.stop();
            }, "CounterCraft-Shutdown"));
            LOG.info("Camera lab listening on 127.0.0.1:{}; single-player only", server.port());
        } catch (Exception e) {
            if (captures != null) captures.close();
            LOG.error("Cannot start camera lab; vanilla camera retained", e);
        }
    }

    public static void tick(MinecraftClient client) {
        if (!enabled) return;
        if (previous != client.world) { previous = client.world; epoch++; STATE.release(); actions.clear("World changed"); controls.release(); }
        boolean offline = client.world != null && client.player != null
                && client.isInSingleplayer() && !client.isPaused();
        Vec3d pos = client.player == null ? Vec3d.ZERO : client.player.getEyePos();
        STATE.update(new BridgeState.World(epoch, offline, pos.x, pos.y, pos.z, System.nanoTime()));
        if (!offline) { STATE.release(); actions.clear("World paused or unavailable"); controls.release(); }
        actions.tick(STATE.world(), System.nanoTime(), action -> PlayerActions.execute(client, action));
        applyInput(client, controls.live(STATE.world(), System.nanoTime()));
        JsonObject player = new JsonObject();
        if (client.player != null) {
            JsonArray eye = new JsonArray(); Vec3d actual = client.player.getEyePos();
            eye.add(actual.x); eye.add(actual.y); eye.add(actual.z); player.add("eye", eye);
            player.addProperty("yaw", client.player.getYaw()); player.addProperty("pitch", client.player.getPitch());
            player.addProperty("health", client.player.getHealth()); player.addProperty("onGround", client.player.isOnGround());
            player.addProperty("usingItem",client.player.isUsingItem());player.addProperty("itemUseTicks",client.player.getItemUseTime());
            player.addProperty("activeItem",net.minecraft.registry.Registries.ITEM.getId(client.player.getActiveItem().getItem()).toString());
            player.addProperty("hunger",client.player.getHungerManager().getFoodLevel());
            player.addProperty("creative",client.player.getAbilities().creativeMode);
            player.addProperty("item",net.minecraft.registry.Registries.ITEM.getId(client.player.getMainHandStack().getItem()).toString());
            player.addProperty("inputUse",previousInput!=null && previousInput.use());
            if(client.crosshairTarget instanceof net.minecraft.util.hit.BlockHitResult hit) {
                JsonArray target=new JsonArray();target.add(hit.getBlockPos().getX());target.add(hit.getBlockPos().getY());target.add(hit.getBlockPos().getZ());
                player.add("target",target);player.addProperty("targetType",hit.getType().toString());player.addProperty("targetFace",hit.getSide().getName());
            }
            player.addProperty("slot", client.player.getInventory().selectedSlot);
            player.addProperty("screen", client.currentScreen == null ? "world" : client.currentScreen.getClass().getSimpleName());
            player.addProperty("inputId", previousInput == null ? -1 : previousInput.id());
        }
        controls.publish(player);
    }

    public static RemoteControl.Input input() {
        var c = MinecraftClient.getInstance();
        return enabled && c.currentScreen == null ? controls.live(STATE.world(), System.nanoTime()) : null;
    }
    public static RemoteControl.Input uiInput() {
        var c = MinecraftClient.getInstance();
        return enabled && c.currentScreen != null ? controls.live(STATE.world(), System.nanoTime()) : null;
    }
    private static void applyInput(MinecraftClient c, RemoteControl.Input next) {
        if (c.player == null) { previousInput = null; return; }
        var invoker = (InteractionInvoker) c;
        if (next == null) {
            if (previousInput != null) { invoker.countercraft$breaking(false); c.player.setSprinting(false); c.player.stopUsingItem(); }
            if(previousInput!=null && c.currentScreen!=null) {
                double x=previousInput.mouseX()*c.getWindow().getScaledWidth(),y=previousInput.mouseY()*c.getWindow().getScaledHeight();
                if(previousInput.attack())c.currentScreen.mouseReleased(x,y,0);
                if(previousInput.use() && c.currentScreen!=null)c.currentScreen.mouseReleased(x,y,1);
            }
            previousInput = null; attackTicks = useTicks = 0; return;
        }
        boolean inventoryEdge = next.inventory() && (previousInput == null || !previousInput.inventory());
        boolean escapeEdge = next.escape() && (previousInput == null || !previousInput.escape());
        if (c.currentScreen != null) {
            if (c.currentScreen instanceof HandledScreen<?> && (inventoryEdge || escapeEdge)) c.player.closeHandledScreen();
            else {
                var screen=c.currentScreen;
                double x = next.mouseX()*c.getWindow().getScaledWidth(), y = next.mouseY()*c.getWindow().getScaledHeight();
                screen.mouseMoved(x, y);
                if(previousInput!=null && next.scroll()!=previousInput.scroll())
                    screen.mouseScrolled(x,y,Math.max(-20,Math.min(20,next.scroll()-previousInput.scroll())));
                if(escapeEdge)screen.keyPressed(256,0,0);
                if (next.attack() && (previousInput == null || !previousInput.attack())) screen.mouseClicked(x, y, 0);
                if (next.use() && (previousInput == null || !previousInput.use())) screen.mouseClicked(x, y, 1);
                if(previousInput!=null) {
                    double dx=(next.mouseX()-previousInput.mouseX())*c.getWindow().getScaledWidth(),dy=(next.mouseY()-previousInput.mouseY())*c.getWindow().getScaledHeight();
                    if(next.attack() && previousInput.attack())screen.mouseDragged(x,y,0,dx,dy);
                    if(next.use() && previousInput.use())screen.mouseDragged(x,y,1,dx,dy);
                }
                if (!next.attack() && previousInput != null && previousInput.attack()) screen.mouseReleased(x, y, 0);
                if (!next.use() && previousInput != null && previousInput.use()) screen.mouseReleased(x, y, 1);
            }
        } else if (c.currentScreen == null) {
            c.player.setYaw(next.yaw()); c.player.setPitch(next.pitch());
            c.player.getInventory().selectedSlot = next.slot(); c.player.setSprinting(next.sprint());
            if(next.drop() && (previousInput==null || !previousInput.drop()))c.player.dropSelectedItem(next.sprint());
            if(next.swap() && (previousInput==null || !previousInput.swap()))c.getNetworkHandler().sendPacket(
                new net.minecraft.network.packet.c2s.play.PlayerActionC2SPacket(
                    net.minecraft.network.packet.c2s.play.PlayerActionC2SPacket.Action.SWAP_ITEM_WITH_OFFHAND,
                    net.minecraft.util.math.BlockPos.ORIGIN,net.minecraft.util.math.Direction.DOWN));
            if(next.pick() && (previousInput==null || !previousInput.pick()))invoker.countercraft$pick();
            if (inventoryEdge) {
                invoker.countercraft$breaking(false); c.setScreen(new InventoryScreen(c.player));
            } else {
                c.gameRenderer.updateTargetedEntity(1);
                if (next.attack()) {
                    if (previousInput == null || !previousInput.attack() || attackTicks++ >= 10) { invoker.countercraft$attack(); attackTicks = 0; }
                    invoker.countercraft$breaking(true);
                } else { invoker.countercraft$breaking(false); attackTicks = 0; }
                if (next.use()) {
                    if (previousInput == null || !previousInput.use() || useTicks++ >= 4) { invoker.countercraft$use(); useTicks = 0; }
                } else {useTicks = 0; if(previousInput != null && previousInput.use())c.player.stopUsingItem();}
            }
        }
        previousInput = next;
    }

    public static void beginFrame() {
        MinecraftClient client = MinecraftClient.getInstance();
        if (enabled) frames.beginFrame(client);
        framePose = enabled && client.world != null && client.world == previous
                && client.isInSingleplayer() && !client.isPaused() ? STATE.live(System.nanoTime()) : null;
    }
    public static BridgeState.Pose framePose() { return framePose; }

    public static boolean keepRunningUnfocused() {
        MinecraftClient client = MinecraftClient.getInstance();
        return enabled && Boolean.getBoolean("countercraft.background") && client.isInSingleplayer();
    }

    private static java.nio.file.Path clientCaptureRoot() {
        return MinecraftClient.getInstance().runDirectory.toPath().resolve("countercraft/captures");
    }

    public static void captureWorld(Camera camera, double fov, MatrixStack matrices) {
        if (enabled) {
            frames.afterWorld(MinecraftClient.getInstance(), camera, fov, matrices, framePose);
            captures.afterWorld(MinecraftClient.getInstance(), camera, fov, matrices, framePose);
        }
    }
    public static void closeRender() {
        if (enabled) frames.closeRender();
    }
    public static void afterClient() { if (enabled) frames.afterClient(MinecraftClient.getInstance()); }
}
