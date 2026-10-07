package dev.countercraft.mixin;
import dev.countercraft.BridgeClient;
import net.minecraft.client.MinecraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(MinecraftClient.class)
public abstract class ClientTickMixin {
    @Inject(method = "stop", at = @At("HEAD"))
    private void countercraft$stop(CallbackInfo ci) { BridgeClient.closeRender(); }

    @Inject(method = "tick", at = @At("TAIL"))
    private void countercraft$tick(CallbackInfo ci) {
        BridgeClient.tick((MinecraftClient) (Object) this);
    }
}
