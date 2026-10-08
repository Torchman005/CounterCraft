// Shared by the protected effect and the standalone hardware GPU oracle.
bool CCMatrixMatches(float4 actual,float4 expected,int matrixIndex) {
    float4 tolerance=matrixIndex<2?float4(1e-5,1e-5,1e-5,1e-5):1e-4+1e-6*abs(expected);
    return !any(isnan(actual)) && !any(isinf(actual)) && all(abs(actual-expected)<=tolerance);
}
bool CCGuestVisible(float2 world, float2 finalDepth, float guestZ, float4 planes,
                    float4 projection, float4 range, bool reversed,bool allowFinalClear) {
    // Preserve post-world writes and ambiguous MSAA edges. NaNs also fail closed.
    bool finalCleared=allowFinalClear && all(finalDepth==range.zz);
    if((any(world != finalDepth) && !finalCleared) || world.x != world.y || any(isnan(world)) || any(isinf(world)))return false;
    if(isnan(guestZ) || isinf(guestZ) || guestZ<0 || guestZ>=1)return false;
    float guestDistance=planes.x*planes.y/(planes.y-guestZ*(planes.y-planes.x));
    float raw=reversed?world.y:world.x;
    if(raw==range.z)return true;
    if(raw<range.x || raw>range.y)return false;
    float ndc=(raw-range.x)/(range.y-range.x);
    float denominator=ndc*projection.z-projection.x;
    if(abs(denominator)<1e-12)return false;
    float hostDistance=-(projection.y-ndc*projection.w)/denominator*range.w;
    return hostDistance>0 && guestDistance<hostDistance;
}
