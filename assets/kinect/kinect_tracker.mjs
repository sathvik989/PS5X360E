// Kinect-style body tracking from one camera (MediaPipe Pose) for the PS5X360 motion bridge.
//
// Kinect reports where a body stands in front of the sensor and how far each hand reaches
// towards it. A phone camera gives a flat picture plus the model's depth guess, which is weak
// exactly where Kinect games need it: punches straight at the camera and steps towards or away
// from it. This tracker adds three estimates on top of the model:
// - body position: distance from the torso's size in the picture, calibrated against the
//   starting distance; sideways and up/down from where the hips sit in the picture;
// - limb depth: arm and leg bones keep their length, so a bone that looks short in the picture
//   points at (or away from) the camera by the missing amount;
// - adaptive smoothing (One Euro): a still body stops shaking, a fast punch keeps its speed.
// Output: the bridge's 20 joints (pose.mjs order) in metres, X picture-right, Y up, Z towards
// the camera, with the console's 2.5 m body distance as Z = 0. The console's NUI conversion
// already places every joint by these values, so no native change is needed.
// All of it is estimated from a single camera; none of it is a Kinect depth measurement.
import {skeleton} from './pose.mjs?v=kinect2';

export const BODY_DISTANCE = 2.5;  // xbox360ps5::motion::kBodyDistance on the console
const TORSO_SEGMENTS = [[11, 12], [23, 24], [11, 23], [12, 24]];
// Chains whose depth is rebuilt from bone length; followers move with the chain's end.
const LIMBS = [
  {chain: [11, 13, 15], followers: [17, 19, 21], weight: .8, arm: true},
  {chain: [12, 14, 16], followers: [18, 20, 22], weight: .8, arm: true},
  {chain: [23, 25, 27], followers: [29, 31], weight: .55},
  {chain: [24, 26, 28], followers: [30, 32], weight: .55},
];
// One Euro settings per bridge joint: [minimum cutoff Hz, speed coefficient].
const SMOOTHING = [
  [1, .6], [1, .6], [1, .6], [1.1, .7],           // hip centre, spine, shoulder centre, head
  [1, .7], [1.3, 1.2], [1.5, 1.6], [1.5, 1.6],    // left shoulder, elbow, wrist, hand
  [1, .7], [1.3, 1.2], [1.5, 1.6], [1.5, 1.6],    // right arm
  [1, .6], [1.2, 1], [1.2, 1.2], [1.2, 1.2],      // left leg
  [1, .6], [1.2, 1], [1.2, 1.2], [1.2, 1.2],      // right leg
];
const HOLD_MS = 400;          // keep a briefly hidden joint where it was, as the sensor infers it
const ROOT_HOLD_MS = 600;     // keep the body position through short torso dropouts
const CALIBRATION_FRAMES = 15, CALIBRATION_MS = 900;

const finite = v => Number.isFinite(v);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const score = v => finite(v) ? clamp(v, 0, 1) : 0;
const vis = p => p ? Math.min(score(p.visibility), p.presence === undefined ? 1 : score(p.presence)) : 0;
const median = a => { const s = [...a].sort((x, y) => x - y); return s[Math.floor(s.length / 2)]; };

// Left/right landmark pairs of MediaPipe Pose.
const PAIRS = [[1, 4], [2, 5], [3, 6], [7, 8], [9, 10], [11, 12], [13, 14], [15, 16], [17, 18], [19, 20],
  [21, 22], [23, 24], [25, 26], [27, 28], [29, 30], [31, 32]];
export const MIRRORED = Array.from({length: 33}, (_, n) => n);
for (const [a, b] of PAIRS) { MIRRORED[a] = b; MIRRORED[b] = a; }
// The pose as a mirror shows it: the picture flips and the sides swap, so the body still faces
// the camera and the physical left hand is reported as the right one. For cameras or setups
// where the game sees left and right the wrong way round.
export function mirrorLandmarks(points, picture) {
  if (!Array.isArray(points) || points.length !== 33) return points;
  return MIRRORED.map(n => ({...points[n], x: picture ? 1 - points[n].x : -points[n].x}));
}

export class OneEuro {
  constructor(minCutoff = 1, beta = .5, dCutoff = 1) { Object.assign(this, {minCutoff, beta, dCutoff}); }
  reset() { this.t = undefined; }
  next(x, t) {
    if (this.t === undefined || !(t > this.t)) {
      if (this.t === undefined) { this.t = t; this.x = x; this.dx = 0; }
      return this.x;
    }
    const dt = (t - this.t) / 1000;
    const a = cutoff => 1 / (1 + 1 / (2 * Math.PI * cutoff * dt));
    this.dx += a(this.dCutoff) * ((x - this.x) / dt - this.dx);
    this.x += a(this.minCutoff + this.beta * Math.abs(this.dx)) * (x - this.x);
    this.t = t;
    return this.x;
  }
}

class BoneLength {
  constructor() { this.samples = []; }
  push(length) { this.samples.push(length); if (this.samples.length > 480) this.samples.shift(); }
  // Picture-plane lengths over about 16 s: a bone turned towards the camera only ever looks
  // shorter, so a high percentile is its length while it lay across the picture.
  value() {
    if (this.samples.length < 15) return null;
    const s = [...this.samples].sort((x, y) => x - y);
    return s[Math.floor(s.length * .9)];
  }
}

export class KinectTracker {
  constructor({startDistance = BODY_DISTANCE, reach = 1, mirror = false} = {}) {
    this.startDistance = startDistance;
    this.reach = reach;
    this.mirror = mirror;
    this.bones = new Map();
    this.signs = new Map();
    this.resetCalibration();
  }
  setStartDistance(metres) {
    if (!finite(metres) || metres < 1 || metres > 3.8) throw new RangeError('Start distance must be 1–3.8 m');
    this.startDistance = metres;
    this.resetCalibration();
  }
  setMirror(enabled) {
    if (Boolean(enabled) === this.mirror) return;
    this.mirror = Boolean(enabled);
    this.bones = new Map();
    this.signs = new Map();
    this.resetCalibration();
  }
  setReach(factor) {
    if (!finite(factor) || factor < 1 || factor > 1.6) throw new RangeError('Reach must be 1–1.6');
    this.reach = factor;
  }
  // Stand still facing the camera again: the current place becomes the centre.
  resetCalibration() {
    this.calibration = null;
    this.samples = [];
    this.root = null;
    this.rootTime = -Infinity;
    this.resetMotion();
  }
  // After a pause in the frames: restart the filters, but keep where the body was, so it
  // does not jump back to the centre for a frame (a game takes such a jump as a new player).
  resetMotion() {
    this.filters = new Map();
    this.rootFilters = [new OneEuro(1, 1.2), new OneEuro(1, 1.2), new OneEuro(.8, 1)];
    this.held = new Map();
    this.strikes = new Map();
    this.lastTime = null;
  }

  // Torso size in pixels per model metre, measured on picture-plane lengths so that turning
  // or leaning shortens both sides alike.
  static torsoScale(world, image, width, height) {
    let pixels = 0, metres = 0, used = 0;
    for (const [a, b] of TORSO_SEGMENTS) {
      if (vis(image[a]) < .5 || vis(image[b]) < .5) continue;
      const m = Math.hypot(world[a].x - world[b].x, world[a].y - world[b].y);
      if (m < .06) continue;
      pixels += Math.hypot((image[a].x - image[b].x) * width, (image[a].y - image[b].y) * height);
      metres += m;
      used++;
    }
    return used >= 3 && metres >= .3 ? pixels / metres : null;
  }

  // Bone-length depth. `p` is the model's world pose in bridge axes (Y up, Z towards camera).
  rebuildLimbs(p) {
    for (const limb of LIMBS) {
      const {chain} = limb;
      const before = p[chain.at(-1)].z;
      const modelZ = chain.map(n => p[n].z);  // model depths, before this chain is rebuilt
      let previousZ = modelZ[0];
      for (let i = 1; i < chain.length; i++) {
        const a = p[chain[i - 1]], b = p[chain[i]], key = chain[i - 1] + '-' + chain[i];
        const dx = b.x - a.x, dy = b.y - a.y, modelDz = modelZ[i] - modelZ[i - 1];
        const flat = Math.hypot(dx, dy);
        if (!this.bones.has(key)) this.bones.set(key, new BoneLength());
        const bone = this.bones.get(key);
        if (a.confidence >= .6 && b.confidence >= .6 && flat > .05) bone.push(flat);
        const length = bone.value();
        let dz = modelDz;
        if (length) {
          const hidden = flat < length ? Math.sqrt(length * length - flat * flat) : 0;
          // The model usually knows which way a bone points even when it misjudges how far.
          let sign = this.signs.get(key) ?? 1;
          if (Math.abs(modelDz) > .03) sign = Math.sign(modelDz);
          this.signs.set(key, sign);
          // Near full picture length the square root turns small noise into large depth:
          // trust the model there, the rebuilt depth once the bone is clearly shortened.
          const k = clamp((.95 - flat / length) / .15, 0, 1);
          dz = modelDz + limb.weight * k * k * (3 - 2 * k) * (sign * hidden - modelDz);
        }
        previousZ += dz;
        b.z = previousZ;
      }
      const shift = p[chain.at(-1)].z - before;
      for (const n of limb.followers) p[n].z += shift;
    }
  }

  // "Punch reach" lengthens an arm only while it strikes forward fast. A slow reach, a push
  // held in front or a guard keeps its real depth, so the game can still tell them apart.
  extendStrikes(p, t) {
    for (const limb of LIMBS.filter(l => l.arm)) {
      const shoulder = p[limb.chain[0]].z, forward = p[limb.chain.at(-1)].z - shoulder;
      const key = limb.chain[0], last = this.strikes.get(key);
      let speed = 0, envelope = 0;
      if (last && t > last.t && t - last.t < 250) {
        const dt = (t - last.t) / 1000;
        speed = last.speed + .5 * ((forward - last.forward) / dt - last.speed);
        // Full strength from 1.8 m/s forward; fades within about a third of a second.
        envelope = Math.max(last.envelope * Math.exp(-dt / .3), clamp((speed - .8) / 1, 0, 1));
      }
      this.strikes.set(key, {t, forward, speed, envelope});
      if (this.reach === 1 || envelope <= 0) continue;
      const factor = 1 + (this.reach - 1) * envelope;
      for (const n of [...limb.chain.slice(1), ...limb.followers])
        if (p[n].z > shoulder) p[n].z = shoulder + (p[n].z - shoulder) * factor;
    }
  }

  // Where the hips are: distance from torso size, sideways/height from the picture position.
  locate(scale, image, width, height, t) {
    const hipsSeen = vis(image[23]) >= .5 && vis(image[24]) >= .5;
    if (!scale || !hipsSeen) return null;
    const u = ((image[23].x + image[24].x) / 2 - .5) * width;
    const v = (.5 - (image[23].y + image[24].y) / 2) * height;
    if (!this.calibration) {
      this.samples.push({t, scale, u, v});
      while (this.samples.length && t - this.samples[0].t > 2500) this.samples.shift();
      const scales = this.samples.map(s => s.scale), mid = median(scales);
      if (Math.max(...scales) / Math.min(...scales) > 1.12) this.samples = [this.samples.at(-1)];
      else if (this.samples.length >= CALIBRATION_FRAMES && t - this.samples[0].t >= CALIBRATION_MS) {
        const focal = mid * this.startDistance;
        this.calibration = {focal, scale: mid, width, height,
          x: median(this.samples.map(s => s.u)) * this.startDistance / focal,
          y: median(this.samples.map(s => s.v)) * this.startDistance / focal};
        this.samples = [];
      }
      return null;
    }
    const c = this.calibration;
    if (width !== c.width || height !== c.height) { this.resetCalibration(); return null; }
    const distance = clamp(c.focal / scale, 1, 3.8);
    return {
      x: clamp(u * distance / c.focal - c.x, -1.6, 1.6),
      y: clamp(v * distance / c.focal - c.y, -1.2, 1.2),
      distance,
    };
  }

  // world/image: MediaPipe worldLandmarks[0] and landmarks[0]; t: capture time in ms.
  update(world, image, t, width, height) {
    if (!Array.isArray(world) || world.length !== 33 || !Array.isArray(image) || image.length !== 33 ||
        !finite(t) || !(width >= 64) || !(height >= 64)) return {pose: null, status: this.status('no_body')};
    if (this.lastTime !== null && t <= this.lastTime) return {pose: null, status: this.status('out_of_order')};
    if (this.lastTime !== null && t - this.lastTime > 250) this.resetMotion();
    this.lastTime = t;
    if (this.mirror) { world = mirrorLandmarks(world, false); image = mirrorLandmarks(image, true); }

    // Model world pose in bridge axes, then bone-length depth on the limbs.
    const p = world.map(q => ({x: q.x, y: -q.y, z: -q.z, confidence: vis(q)}));
    if (p.some(q => ![q.x, q.y, q.z].every(finite))) return {pose: null, status: this.status('no_body')};
    this.rebuildLimbs(p);
    this.extendStrikes(p, t);
    let pose;
    try {
      pose = skeleton(p.map(q => ({x: q.x, y: -q.y, z: -q.z, visibility: q.confidence})), t);
    } catch { return {pose: null, status: this.status('no_body')}; }

    // Hidden joints stay where they were for a moment; then they follow the model as inferred.
    pose.joints.forEach((joint, n) => {
      const held = this.held.get(n);
      if (joint.confidence >= .5) this.held.set(n, {x: joint.x, y: joint.y, z: joint.z, t});
      else if (held && t - held.t < HOLD_MS) Object.assign(joint, {x: held.x, y: held.y, z: held.z});
    });

    // Smooth the body shape (hip-relative), then place it.
    pose.joints.forEach((joint, n) => {
      const [minCutoff, beta] = SMOOTHING[n];
      for (const axis of ['x', 'y', 'z']) {
        const key = n + axis;
        if (!this.filters.has(key)) this.filters.set(key, new OneEuro(axis === 'z' ? minCutoff * .8 : minCutoff, beta));
        joint[axis] = this.filters.get(key).next(joint[axis], t);
      }
    });

    const scale = KinectTracker.torsoScale(world, image, width, height);
    const place = this.locate(scale, image, width, height, t);
    let reason = this.calibration ? 'tracking' : 'calibrating';
    if (place) {
      this.root = {
        x: this.rootFilters[0].next(place.x, t),
        y: this.rootFilters[1].next(place.y, t),
        distance: this.rootFilters[2].next(place.distance, t),
      };
      this.rootTime = t;
    } else if (this.calibration && t - this.rootTime > ROOT_HOLD_MS) {
      reason = 'hips_hidden';
    }
    const root = this.calibration && this.root ? this.root : {x: 0, y: 0, distance: BODY_DISTANCE};
    for (const joint of pose.joints) {
      joint.x += root.x;
      joint.y += root.y;
      joint.z += BODY_DISTANCE - root.distance;
    }
    if (pose.joints.some(j => ![j.x, j.y, j.z].every(finite) || Math.abs(j.x) > 4 || Math.abs(j.y) > 4 || Math.abs(j.z) > 4))
      return {pose: null, status: this.status('out_of_range')};
    pose.space = 'camera-estimated-metres';
    return {pose, status: this.status(reason, root)};
  }

  status(reason, root = null) {
    const progress = this.calibration ? 1 : this.samples.length >= 2 ?
      clamp(Math.min(this.samples.length / CALIBRATION_FRAMES, (this.samples.at(-1).t - this.samples[0].t) / CALIBRATION_MS), 0, 1) : 0;
    return {reason, calibrated: !!this.calibration, progress, root};
  }
}

// Display-only reading of what the skeleton shows. As with a real Kinect, the game recognises
// moves itself from the skeleton; these labels only tell the player what the skeleton carries.
// Each move is told apart by direction, speed, arm shape and height, not by "forward" alone.
const SIDES = [
  {name: 'left', hand: 7, wrist: 6, elbow: 5, shoulder: 4, ankle: 14, foot: 15},
  {name: 'right', hand: 11, wrist: 10, elbow: 9, shoulder: 8, ankle: 18, foot: 19},
];
const minus = (a, b) => ({x: a.x - b.x, y: a.y - b.y, z: a.z - b.z});
const length = v => Math.hypot(v.x, v.y, v.z);
const angleAt = (a, b, c) => {
  const u = minus(a, b), v = minus(c, b), d = length(u) * length(v);
  return d ? Math.acos(clamp((u.x * v.x + u.y * v.y + u.z * v.z) / d, -1, 1)) * 180 / Math.PI : 180;
};

export class GestureMonitor {
  constructor() { this.reset(); }
  reset() {
    this.previous = null; this.velocities = new Map(); this.base = null; this.stepping = {x: false, z: false};
    this.cooldown = new Map(); this.events = []; this.states = new Set();
  }
  fire(name, t, gap = 450) {
    if (t - (this.cooldown.get(name) ?? -Infinity) < gap) return false;
    this.cooldown.set(name, t);
    this.events.push({name, t});
    return true;
  }
  // Smoothed velocity (m/s) of a point; limbs are measured relative to the hips.
  velocity(key, point, t, points) {
    const last = this.previous?.points.get(key);
    let v = this.velocities.get(key) ?? {x: 0, y: 0, z: 0};
    const dt = this.previous ? (t - this.previous.t) / 1000 : 0;
    if (last && dt > 0 && dt < .25) {
      for (const axis of ['x', 'y', 'z']) v = {...v, [axis]: v[axis] + .6 * ((point[axis] - last[axis]) / dt - v[axis])};
    } else v = {x: 0, y: 0, z: 0};
    this.velocities.set(key, v);
    points.set(key, point);
    return v;
  }
  update(pose, t) {
    this.events = this.events.filter(e => t - e.t < 1200);
    this.states.clear();
    if (!pose) { this.previous = null; this.velocities.clear(); return; }
    const j = pose.joints, hip = j[0], head = j[3], points = new Map();
    const body = n => minus(j[n], hip);
    if (!this.base) this.base = {x: hip.x, y: hip.y, z: hip.z, left: j[14].y, right: j[18].y};
    const base = this.base;
    const root = this.velocity('root', hip, t, points);
    const raised = side => j[side.ankle].y - base[side.name];

    const arms = SIDES.map(side => {
      const v = this.velocity(side.name + '.hand', body(side.hand), t, points);
      const forward = j[side.hand].z - j[side.shoulder].z;
      return {side, v, forward, straight: angleAt(j[side.shoulder], j[side.elbow], j[side.wrist]),
        chest: j[side.hand].y > hip.y + .15 && j[side.hand].y < head.y + .25};
    });
    const legs = SIDES.map(side => ({side, v: this.velocity(side.name + '.foot', body(side.foot), t, points)}));

    // Two hands forward together with straightening arms below the face: a push (a ki blast in
    // many games), not two punches. Hands rising to the face with bent elbows are a guard.
    const together = length(minus(j[7], j[11])) < .4;
    const pushing = together && arms.every(a => a.forward > .22 && a.v.z > .8 && a.straight > 120 && a.chest &&
      j[a.side.hand].y < head.y - .08);
    if (pushing && this.fire('two_hand_push', t, 700)) for (const a of arms) this.cooldown.set(a.side.name + '_punch', t);
    for (const a of arms) {
      const {v, side} = a;
      // Punch: fast towards the camera (it may rise on the way, as from a lowered arm), arm
      // straightening, hand at chest/face height.
      if (!pushing && v.z > 1.3 && v.z >= .6 * Math.abs(v.y) && v.z >= 1.2 * Math.abs(v.x) && a.forward > .25 && a.straight > 120 && a.chest)
        this.fire(side.name + '_punch', t);
      // Uppercut: fast upwards, ending at shoulder height or above, in front of the body.
      else if (v.y > 1.8 && v.y > 1.3 * Math.abs(v.z) && j[side.hand].y > j[side.shoulder].y - .05 && a.forward > .05)
        this.fire(side.name + '_uppercut', t, 600);
      // Swipe: fast sideways across the front of the body.
      else if (Math.abs(v.x) > 1.8 && Math.abs(v.x) > 1.4 * Math.max(Math.abs(v.y), Math.abs(v.z)) && a.chest && a.forward > .1)
        this.fire(side.name + '_swipe', t, 600);
    }
    // Kick: one foot fast forward or up while the other stays down (both up is a jump).
    for (const [n, leg] of legs.entries()) {
      const other = SIDES[1 - n], footForward = j[leg.side.foot].z - hip.z;
      if ((footForward > .3 || raised(leg.side) > .25) && Math.hypot(leg.v.y, leg.v.z) > 1.2 && raised(other) < .08)
        this.fire(leg.side.name + '_kick', t, 700);
    }
    if (root.y > .8 && SIDES.every(side => raised(side) > .06)) this.fire('jump', t, 800);

    // Steps are measured from where the player stood. One step is one label: after it fires,
    // the reference follows the body until it stops moving that way.
    const away = hip.z - base.z, aside = hip.x - base.x;
    if (this.stepping.z && Math.abs(root.z) < .25) this.stepping.z = false;
    if (this.stepping.x && Math.abs(root.x) < .25) this.stepping.x = false;
    if (this.stepping.z) base.z = hip.z;
    else if (Math.abs(away) > .2 && this.fire(away < 0 ? 'step_back' : 'step_forward', t, 300)) { base.z = hip.z; this.stepping.z = true; }
    if (this.stepping.x) base.x = hip.x;
    else if (Math.abs(aside) > .25 && this.fire('step_side', t, 300)) { base.x = hip.x; this.stepping.x = true; }
    const dt = this.previous ? Math.max(0, (t - this.previous.t) / 1000) : 0, follow = k => 1 - Math.exp(-dt / k);
    base.x += follow(3) * (hip.x - base.x);
    base.z += follow(3) * (hip.z - base.z);
    // Height and feet references follow only while standing calmly on the floor.
    if (Math.abs(root.y) < .3 && hip.y - base.y > -.1 && hip.y - base.y < .1) {
      base.y += follow(4) * (hip.y - base.y);
      for (const side of SIDES) base[side.name] += follow(4) * (j[side.ankle].y - base[side.name]);
    }

    if (arms.every(a => j[a.side.hand].y > j[2].y - .05 && Math.abs(j[a.side.hand].x - head.x) < .3 &&
        a.forward > .1 && j[a.side.elbow].y < j[a.side.hand].y)) this.states.add('guard');
    else if (arms.every(a => j[a.side.hand].y > head.y + .05)) this.states.add('hands_up');
    if (hip.y - base.y < -.22) this.states.add('crouch');
    const lean = minus(j[2], hip);
    if (lean.z > .15) this.states.add('lean_forward');
    else if (lean.z < -.12) this.states.add('lean_back');
    if (Math.abs(lean.x) > .14) this.states.add('lean_side');
    this.previous = {t, points};
  }
}
