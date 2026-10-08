package dev.countercraft;

import com.google.gson.*;
import net.minecraft.client.MinecraftClient;
import net.minecraft.entity.MovementType;
import net.minecraft.registry.Registries;
import net.minecraft.screen.slot.SlotActionType;
import net.minecraft.util.Hand;
import net.minecraft.util.Identifier;
import net.minecraft.item.ItemStack;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.hit.HitResult;
import net.minecraft.util.math.*;
import net.minecraft.world.RaycastContext;

/** Called exclusively by BridgeClient.tick on the client thread. */
public final class PlayerActions {
    private PlayerActions() {}
    public static JsonObject execute(MinecraftClient c, ActionQueue.Action a) {
        if (!c.isOnThread() || c.player == null || c.world == null || c.interactionManager == null
                || !c.isInSingleplayer() || c.isPaused()) throw new IllegalArgumentException("Player unavailable");
        var p = c.player;
        JsonObject result = new JsonObject();
        switch (a.kind()) {
            case "target" -> {
                double reach = c.interactionManager.getReachDistance();
                var ray = c.world.raycast(new RaycastContext(p.getEyePos(), p.getEyePos().add(p.getRotationVec(1).multiply(reach)),
                        RaycastContext.ShapeType.OUTLINE, RaycastContext.FluidHandling.NONE, p));
                result.addProperty("hit", ray.getType() == HitResult.Type.BLOCK);
                if (ray.getType() == HitResult.Type.BLOCK) {
                    var pos = ray.getBlockPos();
                    JsonArray block = new JsonArray(); block.add(pos.getX()); block.add(pos.getY()); block.add(pos.getZ());
                    result.add("position", block); result.addProperty("face", ray.getSide().getName());
                    result.addProperty("block", Registries.BLOCK.getId(c.world.getBlockState(pos).getBlock()).toString());
                }
            }
            case "look" -> { p.setYaw(a.yaw()); p.setPitch(a.pitch()); }
            case "move" -> {
                if (c.currentScreen != null) throw new IllegalArgumentException("Close the current screen before moving");
                // Entity.move resolves Minecraft block collisions; no teleport or noclip.
                p.move(MovementType.SELF, new Vec3d(a.x(), a.y(), a.z()));
            }
            case "select" -> p.getInventory().selectedSlot = a.slot();
            case "creative" -> {
                if (!p.getAbilities().creativeMode || !c.interactionManager.hasCreativeInventory())
                    throw new IllegalArgumentException("Creative inventory requires creative game mode");
                var id = new Identifier(a.item());
                if (!Registries.ITEM.containsId(id)) throw new IllegalArgumentException("Unknown item id");
                var stack = new ItemStack(Registries.ITEM.get(id), a.count());
                if (stack.isEmpty() || a.count() > stack.getMaxCount()) throw new IllegalArgumentException("Invalid stack size");
                p.getInventory().setStack(a.slot(), stack);
                c.interactionManager.clickCreativeStack(stack, 36 + a.slot());
                result.addProperty("submitted", true);
            }
            case "inventory" -> {
                if (p.currentScreenHandler != p.playerScreenHandler) throw new IllegalArgumentException("Close container before inventory access");
                JsonArray slots = new JsonArray();
                for (var slot : p.playerScreenHandler.slots) {
                    JsonObject item = new JsonObject();
                    item.addProperty("slot", slot.id); item.addProperty("item", Registries.ITEM.getId(slot.getStack().getItem()).toString());
                    item.addProperty("count", slot.getStack().getCount()); slots.add(item);
                }
                result.add("slots", slots);
                result.addProperty("cursorItem", Registries.ITEM.getId(p.playerScreenHandler.getCursorStack().getItem()).toString());
                result.addProperty("cursorCount", p.playerScreenHandler.getCursorStack().getCount());
            }
            case "click" -> {
                if (p.currentScreenHandler != p.playerScreenHandler) throw new IllegalArgumentException("Close container before inventory access");
                c.interactionManager.clickSlot(p.playerScreenHandler.syncId, a.slot(), a.button(), SlotActionType.PICKUP, p);
                result.addProperty("submitted", true);
            }
            case "inspect", "break", "place" -> {
                if (c.currentScreen != null) throw new IllegalArgumentException("Close the current screen before world actions");
                BlockPos pos = new BlockPos((int) a.x(), (int) a.y(), (int) a.z());
                if (!c.world.isInBuildLimit(pos) || !c.world.getWorldBorder().contains(pos)
                        || !c.world.isChunkLoaded(pos)) throw new IllegalArgumentException("Block is outside loaded world bounds");
                double reach = c.interactionManager.getReachDistance();
                if (p.getEyePos().squaredDistanceTo(Vec3d.ofCenter(pos)) > (reach + 0.87) * (reach + 0.87))
                    throw new IllegalArgumentException("Block is outside player reach");
                if (!a.kind().equals("inspect")) {
                    Direction side = Direction.byName(a.face());
                    BlockPos support = a.kind().equals("place") ? pos.offset(side.getOpposite()) : pos;
                    if (!c.world.isInBuildLimit(support) || !c.world.getWorldBorder().contains(support)
                            || !c.world.isChunkLoaded(support)) throw new IllegalArgumentException("Support is outside loaded world bounds");
                    if (a.kind().equals("place") && !c.world.isAir(pos)) throw new IllegalArgumentException("Placement target must be air");
                    Vec3d hit = Vec3d.ofCenter(support).add(Vec3d.of(side.getVector()).multiply(0.5));
                    var ray = c.world.raycast(new RaycastContext(p.getEyePos(), hit.add(Vec3d.of(side.getVector()).multiply(-0.001)),
                            RaycastContext.ShapeType.OUTLINE, RaycastContext.FluidHandling.NONE, p));
                    if (ray.getType() != HitResult.Type.BLOCK || !ray.getBlockPos().equals(support) || ray.getSide() != side
                            || p.getEyePos().squaredDistanceTo(hit) > reach * reach)
                        throw new IllegalArgumentException("Block face is not visible within reach: " + ray.getType() + " " + ray.getBlockPos() + " " + ray.getSide());
                    if (a.kind().equals("break")) {
                        boolean accepted = c.interactionManager.isBreakingBlock()
                                ? c.interactionManager.updateBlockBreakingProgress(pos, side)
                                : c.interactionManager.attackBlock(pos, side);
                        result.addProperty("submitted", accepted);
                    } else {
                        var accepted = c.interactionManager.interactBlock(p, Hand.MAIN_HAND, new BlockHitResult(hit, side, support, false));
                        result.addProperty("submitted", accepted.isAccepted());
                    }
                    p.swingHand(Hand.MAIN_HAND);
                }
                result.addProperty("block", Registries.BLOCK.getId(c.world.getBlockState(pos).getBlock()).toString());
                result.addProperty("air", c.world.isAir(pos));
            }
            default -> throw new IllegalArgumentException("Unknown action");
        }
        JsonArray position = new JsonArray();
        position.add(p.getX()); position.add(p.getY()); position.add(p.getZ());
        result.add("feet", position);
        result.addProperty("yaw", p.getYaw()); result.addProperty("pitch", p.getPitch());
        result.addProperty("selectedSlot", p.getInventory().selectedSlot);
        result.addProperty("health", p.getHealth()); result.addProperty("onGround", p.isOnGround());
        return result;
    }
}
