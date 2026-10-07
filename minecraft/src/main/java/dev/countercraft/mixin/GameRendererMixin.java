package dev.countercraft.mixin;
import dev.countercraft.BridgeClient;
import dev.countercraft.BridgeState;
import net.minecraft.client.render.GameRenderer;
import net.minecraft.client.render.Camera;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.util.math.MatrixStack;
import net.minecraft.client.option.GameOptions;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {
    @Shadow private double getFov(Camera camera, float tickDelta, boolean changingFov) { throw new AssertionError(); }
    @Inject(method = "render", at = @At("HEAD"))
    private void countercraft$frame(CallbackInfo ci) { BridgeClient.beginFrame(); }

    @Redirect(method = "render", at = @At(value = "FIELD",
            target = "Lnet/minecraft/client/option/GameOptions;pauseOnLostFocus:Z"))
    private boolean countercraft$background(GameOptions options) {
        return !BridgeClient.keepRunningUnfocused() && options.pauseOnLostFocus;
    }

    @Inject(method = "getFov", at = @At("HEAD"), cancellable = true)
    private void countercraft$fov(CallbackInfoReturnable<Double> cir) {
        BridgeState.Pose pose = BridgeClient.framePose();
        if (pose != null) cir.setReturnValue((double) pose.fov());
    }

    // Read world colour/depth before vanilla clears depth for the hand and HUD.
    @Inject(method = "renderWorld", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/render/WorldRenderer;render(Lnet/minecraft/client/util/math/MatrixStack;FJZLnet/minecraft/client/render/Camera;Lnet/minecraft/client/render/GameRenderer;Lnet/minecraft/client/render/LightmapTextureManager;Lorg/joml/Matrix4f;)V",
            shift = At.Shift.AFTER))
    private void countercraft$capture(float tickDelta, long limitTime, MatrixStack matrices, CallbackInfo ci) {
        Camera camera = MinecraftClient.getInstance().gameRenderer.getCamera();
        BridgeClient.captureWorld(camera, getFov(camera, tickDelta, true), matrices);
    }
}
