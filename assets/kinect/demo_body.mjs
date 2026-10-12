// A synthetic body seen by a pinhole camera, shaped like MediaPipe Pose output: picture-plane
// coordinates are right, depth is underestimated (x0.4) and every value carries noise.
// Used by check-kinect.mjs and by the page's ?demo mode (no camera, no model download).

// Standing body, bridge axes (X picture-right = the person's left, Y up, Z towards the camera),
// hips at the origin; MediaPipe landmark numbers.
export const STAND = {
  0: [0, .62, .08], 1: [.03, .66, .06], 2: [.04, .66, .06], 3: [.05, .66, .06], 4: [-.03, .66, .06],
  5: [-.04, .66, .06], 6: [-.05, .66, .06], 7: [.08, .63, 0], 8: [-.08, .63, 0], 9: [.02, .57, .07], 10: [-.02, .57, .07],
  11: [.19, .48, 0], 12: [-.19, .48, 0], 13: [.22, .2, 0], 14: [-.22, .2, 0], 15: [.23, -.06, 0], 16: [-.23, -.06, 0],
  17: [.24, -.14, 0], 18: [-.24, -.14, 0], 19: [.22, -.15, .02], 20: [-.22, -.15, .02], 21: [.21, -.12, .03], 22: [-.21, -.12, .03],
  23: [.1, 0, 0], 24: [-.1, 0, 0], 25: [.11, -.45, 0], 26: [-.11, -.45, 0], 27: [.11, -.88, 0], 28: [-.11, -.88, 0],
  29: [.11, -.92, -.05], 30: [-.11, -.92, -.05], 31: [.11, -.93, .1], 32: [-.11, -.93, .1],
};
// Guard: both fists up in front of the face, elbows down.
const GUARD = {13: [.2, .25, .15], 14: [-.2, .25, .15], 15: [.1, .5, .28], 16: [-.1, .5, .28],
  17: [.08, .56, .32], 18: [-.08, .56, .32], 19: [.06, .55, .33], 20: [-.06, .55, .33], 21: [.07, .52, .32], 22: [-.07, .52, .32]};
// Right arm (the person's right, picture-left) punching straight at the camera.
export const PUNCH_RIGHT = {14: [-.19, .47, .28], 16: [-.19, .46, .54], 18: [-.2, .44, .62], 20: [-.18, .46, .63], 22: [-.17, .47, .6]};
const PUNCH_LEFT = Object.fromEntries(Object.entries(PUNCH_RIGHT).map(([n, [x, y, z]]) => [Number(n) - 1, [-x, y, z]]));
// Both hands pushed forward together at chest height.
export const PUSH = {13: [.17, .36, .2], 14: [-.17, .36, .2], 15: [.07, .4, .45], 16: [-.07, .4, .45],
  17: [.06, .41, .53], 18: [-.06, .41, .53], 19: [.04, .42, .54], 20: [-.04, .42, .54], 21: [.06, .44, .5], 22: [-.06, .44, .5]};
// Right front kick: thigh up and forward, shin out towards the camera.
export const KICK_RIGHT = {26: [-.11, -.2, .38], 28: [-.11, -.25, .8], 30: [-.11, -.3, .82], 32: [-.11, -.22, .9]};

export const blend = (a, b, k) => Object.fromEntries(Object.keys(a).map(n => [n, a[n].map((v, i) => v + k * ((b[n] ?? a[n])[i] - v))]));

let seed = 7;
const noise = amount => { seed = (seed * 16807) % 2147483647; return (seed / 2147483647 - .5) * 2 * amount; };

// place: {x, y, distance} of the hips in front of a camera with focal length `focal` pixels.
export function observe(body, place, {width = 640, height = 480, focal = 520, jitter = .004, hidden = []} = {}) {
  const world = [], image = [];
  for (let n = 0; n < 33; n++) {
    const [x, y, z] = body[n];
    const visibility = hidden.includes(n) ? .2 : .95;
    world.push({x: x + noise(jitter), y: -(y + noise(jitter)), z: -(z * .4 + noise(jitter * 3)), visibility});
    const cx = place.x + x, cy = place.y + y + .1, depth = place.distance - z;
    image.push({x: (width / 2 + focal * cx / depth) / width + noise(jitter / 4),
      y: (height / 2 - focal * cy / depth) / height + noise(jitter / 4), z: 0, visibility});
  }
  return {world, image};
}

// A looping 17 s routine for the page demo and the checks: guard, right and left punches, a
// two-hand push, a step back and forward, a side step, a jump, a right kick, and a slow
// forward reach with the right hand (which must not read as a punch).
export const DEMO_SECONDS = 17;
export function demoFrame(seconds) {
  const s = seconds % DEMO_SECONDS, smooth = k => k * k * (3 - 2 * k), pulse = (a, b) => s < a || s > b ? 0 :
    Math.sin(Math.PI * (s - a) / (b - a));
  let body = STAND;
  if (s >= 1 && s < 2.5) body = blend(STAND, {...STAND, ...GUARD}, pulse(1, 2.5) ** .4);
  if (s >= 2.8 && s < 3.4) body = blend(STAND, {...STAND, ...PUNCH_RIGHT}, pulse(2.8, 3.4));
  if (s >= 3.6 && s < 4.2) body = blend(STAND, {...STAND, ...PUNCH_LEFT}, pulse(3.6, 4.2));
  if (s >= 4.5 && s < 5.2) body = blend(STAND, {...STAND, ...PUSH}, pulse(4.5, 5.2));
  if (s >= 12.5 && s < 13.3) body = blend(STAND, {...STAND, ...KICK_RIGHT}, pulse(12.5, 13.3));
  if (s >= 13.5) body = blend(STAND, {...STAND, ...PUNCH_RIGHT}, pulse(13.5, 17) * .9);  // about 0.5 m/s
  const back = s < 5.5 ? 0 : s < 6.5 ? smooth(s - 5.5) : s < 8.5 ? 1 : s < 9.5 ? 1 - smooth(s - 8.5) : 0;
  const side = s < 10 ? 0 : s < 11 ? Math.sin(Math.PI * (s - 10)) * .35 : 0;
  const jump = pulse(11.5, 12) * .25;
  return observe(body, {x: side, y: jump, distance: 2.5 + .6 * back}, {jitter: .003});
}
