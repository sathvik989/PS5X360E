// MediaPipe worldLandmarks only: estimated metres, not normalized image points.
// Anatomical NUI order: hip/spine/shoulder centre/head, left arm, right arm,
// left leg, right leg. Hands are the index/pinky knuckle midpoint (17+19, 18+20),
// near the palm centre that NUI reports; still an estimate, not palm tracking.
// Input video is unmirrored. Keep picture-right X; the native SkeletonSpace
// flips X once. Convert image-down Y and camera-away Z here, without swapping
// physical left/right joints. A mirrored preview must never alter these data.
const mapping = [[23,24],[11,12],[11,12],[7,8],11,13,15,[17,19],12,14,16,[18,20],23,25,27,31,24,26,28,32];
const score = value => Number.isFinite(value) ? Math.max(0,Math.min(1,value)) : 0;
const confidence = point => Math.min(score(point.visibility),
  point.presence === undefined ? 1 : score(point.presence));
export function skeleton(points, timestamp) {
  if (!Array.isArray(points) || points.length !== 33 || !Number.isFinite(timestamp)) throw Error('Invalid pose');
  const joints = mapping.map(index => {
    const ids = Array.isArray(index) ? index : [index];
    const values = ids.map(i => points[i]);
    if (values.some(p => !p || ![p.x,p.y,p.z].every(Number.isFinite))) throw Error('Invalid joint');
    return {x:values.reduce((n,p)=>n+p.x,0)/ids.length,
      y:-values.reduce((n,p)=>n+p.y,0)/ids.length,
      z:-values.reduce((n,p)=>n+p.z,0)/ids.length,
      confidence:Math.min(...values.map(confidence))};
  });
  // Model estimates need not have an exactly zero hip midpoint. Enforce the
  // bridge's advertised hip-relative origin without rescaling limb movement.
  const origin = {...joints[0]};
  for (const joint of joints) for (const axis of ['x','y','z']) joint[axis] -= origin[axis];
  for (const axis of ['x','y','z']) joints[1][axis] = (joints[0][axis]+joints[2][axis])/2;
  joints[1].confidence = Math.min(joints[0].confidence,joints[2].confidence);
  if (joints.some(joint => ['x','y','z'].some(axis => !Number.isFinite(joint[axis]) || Math.abs(joint[axis]) > 4))) throw Error('Pose outside relay bounds');
  return {protocol:'ps5x360-phone-pose',version:1,timestamp,space:'hip-relative-estimated-metres',joints};
}
