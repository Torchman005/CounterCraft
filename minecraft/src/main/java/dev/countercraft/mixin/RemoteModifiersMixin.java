package dev.countercraft.mixin;
import dev.countercraft.BridgeClient;
import net.minecraft.client.gui.screen.Screen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Screen.class)
public abstract class RemoteModifiersMixin {
    @Inject(method="hasShiftDown",at=@At("HEAD"),cancellable=true)
    private static void countercraft$shift(CallbackInfoReturnable<Boolean> result) {
        var input=BridgeClient.uiInput();if(input!=null)result.setReturnValue(input.sneak());
    }
    @Inject(method="hasControlDown",at=@At("HEAD"),cancellable=true)
    private static void countercraft$control(CallbackInfoReturnable<Boolean> result) {
        var input=BridgeClient.uiInput();if(input!=null)result.setReturnValue(input.sprint());
    }
}
