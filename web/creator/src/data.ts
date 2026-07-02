// Creator tabs + the discrete slider option-sets. Counts come from the AvatarPreset
// registry (per gender): HairStyle 52, HairColor/EyebrowColor 32, EyeColor 25, SkinColor
// 24, EyebrowShape 20, HeadStyle(face) 15, FaceMarking sets ~13-16. A few (glasses,
// freckles/scars exact split) are best-guess until the index->preset catalog is wired on
// the C++ side; they're easy to tweak here.

export interface Section {
  key: string; // category id sent to the host as cc:option { category, index }
  label: string;
  max: number; // number of discrete options (slider goes 1..max)
}

// Framing camera per tab (front view onto the live avatar). Tunable live — pawn origin is
// ~mid-torso, so +height looks up toward the head; dist/fov in cm/degrees.
export interface Camera {
  dist: number;
  height: number;
  pitch: number;
  fov: number;
  shift: number; // manual nudge (cm) added to the auto-derived lateral offset; usually 0
}

export interface Tab {
  id: string;
  title: string;
  numeral: string;
  finalise?: boolean;
  sections: Section[];
  camera: Camera;
}

export const TABS: Tab[] = [
  { id: "presets", title: "Presets", numeral: "I", sections: [{ key: "preset", label: "Preset", max: 30 }], camera: { dist: 290, height: 80, pitch: -5, fov: 36, shift: 0 } },
  {
    id: "facewear",
    title: "Facewear",
    numeral: "II",
    sections: [
      { key: "faceShape", label: "Face Shape", max: 15 },
      { key: "skin", label: "Skin Tone", max: 24 },
      { key: "eyeColour", label: "Eye Colour", max: 25 },
      { key: "glasses", label: "Glasses", max: 6 },
    ],
    camera: { dist: 160, height: 77, pitch: -2, fov: 36, shift: 0 },
  },
  {
    id: "hair",
    title: "Hairstyles",
    numeral: "III",
    sections: [
      { key: "hairStyle", label: "Hair Style", max: 52 },
      { key: "hairColour", label: "Hair Colour", max: 32 },
    ],
    camera: { dist: 170, height: 80, pitch: -3, fov: 26, shift: 0 },
  },
  {
    id: "complexion",
    title: "Complexion",
    numeral: "IV",
    sections: [
      { key: "marking1", label: "Complexion", max: 16 },
      { key: "marking0", label: "Freckles and Moles", max: 13 },
      { key: "marking2", label: "Scars and Markings", max: 14 },
    ],
    camera: { dist: 170, height: 75, pitch: -2, fov: 26, shift: 0 },
  },
  {
    id: "eyebrows",
    title: "Eyebrows",
    numeral: "V",
    sections: [
      { key: "browShape", label: "Brow Shape", max: 20 },
      { key: "browColour", label: "Brow Colour", max: 32 },
    ],
    camera: { dist: 170, height: 75, pitch: -2, fov: 26, shift: 0 },
  },
  { id: "finalise", title: "Finalise", numeral: "VI", finalise: true, sections: [], camera: { dist: 405, height: 65, pitch: -4, fov: 36, shift: 0 } },
];

// Voice/pitch on the Finalise tab. Pitch range is a placeholder (the design assumed 9) —
// the real range comes from the in-game AvaAudio::SetPlayerVoicePitch probe.
export const VOICE_TONES = 2;
export const PITCH_MAX = 9;
