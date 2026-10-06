package dev.countercraft.mixin;
import dev.countercraft.BridgeClient;
import dev.countercraft.BridgeState;
import net.minecraft.client.render.Camera;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Camera.class)
public abstract class CameraMixin {
    @Shadow protected abstract void setPos(double x, double y, double z);
    @Shadow protected abstract void setRotation(float yaw, float pitch);

    @Inject(method = "update", at = @At("TAIL"))
    private void countercraft$camera(CallbackInfo ci) {
        BridgeState.Pose pose = BridgeClient.framePose();
        if (pose != null) {
            setPos(pose.x(), pose.y(), pose.z());
            setRotation(pose.yaw(), pose.pitch());
        }
    }
}
