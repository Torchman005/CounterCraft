package dev.countercraft.mixin;
import dev.countercraft.BridgeClient;
import dev.countercraft.BridgeState;
import net.minecraft.client.render.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {
    @Inject(method = "render", at = @At("HEAD"))
    private void countercraft$frame(CallbackInfo ci) { BridgeClient.beginFrame(); }

    @Inject(method = "getFov", at = @At("HEAD"), cancellable = true)
    private void countercraft$fov(CallbackInfoReturnable<Double> cir) {
        BridgeState.Pose pose = BridgeClient.framePose();
        if (pose != null) cir.setReturnValue((double) pose.fov());
    }
}
