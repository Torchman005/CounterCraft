package dev.countercraft.mixin;
import dev.countercraft.BridgeClient;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.option.KeyBinding;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Vanilla item-use and block-progress loops must see the held remote buttons. */
@Mixin(KeyBinding.class)
public abstract class RemoteKeyMixin {
    @Inject(method="isPressed",at=@At("HEAD"),cancellable=true)
    private void countercraft$held(CallbackInfoReturnable<Boolean> result) {
        var input=BridgeClient.input();
        if(input==null)return;
        var options=MinecraftClient.getInstance().options;
        if((Object)this==options.attackKey)result.setReturnValue(input.attack());
        if((Object)this==options.useKey)result.setReturnValue(input.use());
    }
}
