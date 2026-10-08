package dev.countercraft.mixin;
import dev.countercraft.BridgeClient;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.Mouse;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Mouse.class)
public abstract class RemoteMouseMixin {
    @Inject(method="getX",at=@At("HEAD"),cancellable=true)
    private void countercraft$x(CallbackInfoReturnable<Double> result) {
        var input=BridgeClient.uiInput();
        if(input!=null)result.setReturnValue(input.mouseX()*MinecraftClient.getInstance().getWindow().getWidth());
    }
    @Inject(method="getY",at=@At("HEAD"),cancellable=true)
    private void countercraft$y(CallbackInfoReturnable<Double> result) {
        var input=BridgeClient.uiInput();
        if(input!=null)result.setReturnValue(input.mouseY()*MinecraftClient.getInstance().getWindow().getHeight());
    }
}
