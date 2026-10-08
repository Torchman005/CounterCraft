package dev.countercraft.mixin;
import dev.countercraft.BridgeClient;
import net.minecraft.client.input.KeyboardInput;
import net.minecraft.client.input.Input;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(KeyboardInput.class)
public abstract class RemoteKeyboardMixin extends Input {
    @Inject(method="tick", at=@At("TAIL"))
    private void countercraft$input(boolean slowDown, float factor, CallbackInfo ci) {
        var remote = BridgeClient.input();
        if (remote == null) return;
        pressingForward = remote.forward()>0; pressingBack = remote.forward()<0;
        pressingLeft = remote.sideways()>0; pressingRight = remote.sideways()<0;
        movementForward = remote.forward()*(slowDown?factor:1);
        movementSideways = remote.sideways()*(slowDown?factor:1);
        jumping = remote.jump(); sneaking = remote.sneak();
    }
}
